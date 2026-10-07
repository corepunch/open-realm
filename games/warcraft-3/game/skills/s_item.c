#include "s_skills.h"

#define ID_ITEM_HEAL           MAKEFOURCC('A', 'I', 'h', 'e')
#define ID_ITEM_MANA           MAKEFOURCC('A', 'I', 'm', 'a')
#define ID_ITEM_LIFE_GAIN      MAKEFOURCC('A', 'I', 'm', 'i')
#define ID_ITEM_PERM_STR       MAKEFOURCC('A', 'I', 's', 'm')
#define ID_ITEM_PERM_AGI       MAKEFOURCC('A', 'I', 'a', 'm')
#define ID_ITEM_PERM_INT       MAKEFOURCC('A', 'I', 'i', 'm')
#define ID_ITEM_PERM_MULTI     MAKEFOURCC('A', 'I', 'x', 'm')
#define ID_ITEM_XP_GAIN        MAKEFOURCC('A', 'I', 'e', 'm')
#define ID_ITEM_LEVEL_GAIN     MAKEFOURCC('A', 'I', 'l', 'm')
#define ID_ITEM_FIGURINE       MAKEFOURCC('A', 'I', 'f', 's')
#define ID_ITEM_DEFENSE_AOE    MAKEFOURCC('A', 'I', 'd', 'a')
#define ID_ITEM_CHANGE_TIME    MAKEFOURCC('A', 'I', 'c', 't')
#define ID_SOUL_TRAP           MAKEFOURCC('A', 'I', 's', 'o')
#define ID_SOUL_POSSESSION     MAKEFOURCC('A', 's', 'o', 'u')
#define ID_FILLED_SOUL         MAKEFOURCC('s', 'o', 'u', 'l')

/* ---- Active items (consume on use) -------------------------------------- */

static bool soul_trap_valid_link(edict_t const *carrier, edict_t const *target) {
    return carrier && carrier->inuse && target && target->inuse &&
        target->soul_trap_carrier == carrier &&
        target->soul_trap_carrier_spawn_time == carrier->spawn_time &&
        (target->aiflags & AI_SOUL_TRAPPED);
}

static void soul_trap_remove_possession(edict_t *carrier) {
    if (!carrier || !carrier->soul_possession_added || carrier->soul_trap_head) return;
    carrier->soul_possession_added = false;
    if (G_ActorHasSkill(carrier, "Asou"))
        G_ActorRemoveSkill(carrier, ID_SOUL_POSSESSION);
}

static void soul_trap_unlink(edict_t *target) {
    edict_t *carrier, *current, *previous = NULL;
    uint32_t current_spawn, carrier_spawn, guard = 0;
    if (!target) return;
    carrier = target->soul_trap_carrier;
    carrier_spawn = target->soul_trap_carrier_spawn_time;
    if (carrier && carrier->inuse && carrier->spawn_time == carrier_spawn) {
        current = carrier->soul_trap_head;
        current_spawn = carrier->soul_trap_head_spawn_time;
        while (current && guard++ < globals.max_edicts) {
            edict_t *next;
            uint32_t next_spawn;
            if (!current->inuse || current->spawn_time != current_spawn) break;
            next = current->soul_trap_next;
            next_spawn = current->soul_trap_next_spawn_time;
            if (current == target) {
                if (previous) {
                    previous->soul_trap_next = next;
                    previous->soul_trap_next_spawn_time = next_spawn;
                } else {
                    carrier->soul_trap_head = next;
                    carrier->soul_trap_head_spawn_time = next_spawn;
                }
                break;
            }
            previous = current;
            current = next;
            current_spawn = next_spawn;
        }
    }
    target->soul_trap_carrier = target->soul_trap_next = NULL;
    target->soul_trap_carrier_spawn_time = target->soul_trap_next_spawn_time = 0;
    target->aiflags &= ~AI_SOUL_TRAPPED;
    if (carrier && carrier->inuse && carrier->spawn_time == carrier_spawn) {
        G_RemoveUnitForcedVisibility(carrier, target->soul_trap_viewer);
        soul_trap_remove_possession(carrier);
    }
    target->soul_trap_viewer = 0;
}

static void soul_trap_release_target(edict_t *target, vec2_t const *position, bool restore_world) {
    bool remove_asou;
    edict_t *item;
    uint32_t item_spawn;
    if (!target || !(target->aiflags & AI_SOUL_TRAPPED)) return;
    soul_trap_unlink(target);
    item = target->soul_trap_item;
    item_spawn = target->soul_trap_item_spawn_time;
    target->soul_trap_item = NULL;
    target->soul_trap_item_spawn_time = 0;
    if (item && item->inuse && item->spawn_time == item_spawn &&
        item->item->soul_target == target && item->item->soul_target_spawn_time == target->spawn_time) {
        item->item->soul_target = NULL;
        item->item->soul_target_spawn_time = 0;
        G_RemoveItem(item);
    }
    remove_asou = target->soul_trapped_ability_added;
    target->soul_trapped_ability_added = false;
    if (restore_world && !M_IsDead(target)) {
        if (position) {
            target->s.origin2 = *position;
            target->s.origin.x = position->x;
            target->s.origin.y = position->y;
            target->s.origin.z = CM_GetHeightAtPoint(position->x, position->y);
        }
        target->s.renderfx &= ~RF_HIDDEN;
        target->svflags &= ~SVF_NOCLIENT;
        target->s.flags &= ~EF_NOT_SELECTABLE;
        if (target->stand) target->stand(target);
        gi.LinkEntity(target);
        if (target->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
        if (G_UnitIsStructure(target)) CM_BakeStaticObstacles();
    }
    if (remove_asou && G_ActorHasSkill(target, "Asou"))
        G_ActorRemoveSkill(target, ID_SOUL_POSSESSION);
}

static void soul_trap_release_carried(edict_t *carrier, vec2_t const *position, bool restore_world) {
    uint32_t guard = 0;
    if (!carrier) return;
    while (carrier->soul_trap_head && guard++ < globals.max_edicts) {
        edict_t *target = carrier->soul_trap_head;
        uint32_t spawn_time = carrier->soul_trap_head_spawn_time;
        if (!target->inuse || target->spawn_time != spawn_time) {
            carrier->soul_trap_head = target->soul_trap_next;
            carrier->soul_trap_head_spawn_time = target->soul_trap_next_spawn_time;
            continue;
        }
        soul_trap_release_target(target, position, restore_world && target != carrier);
    }
    soul_trap_remove_possession(carrier);
}

static bool soul_trap_capture(edict_t *carrier, edict_t *target) {
    uint32_t const code = ID_SOUL_POSSESSION;
    if (!carrier || !carrier->inuse || M_IsDead(carrier) || !target || !target->inuse ||
        M_IsDead(target) || (target->aiflags & AI_SOUL_TRAPPED)) return false;
    if (!G_ActorHasSkill(carrier, "Asou")) {
        if (!G_ActorAddSkill(carrier, code)) {
            fprintf(stderr, "Soul Trap: unable to add Asou possession state to carrier %.4s\n",
                    (cstring_t)&carrier->class_id);
            return false;
        }
        carrier->soul_possession_added = true;
    }
    if (target != carrier && !G_ActorHasSkill(target, "Asou")) {
        if (!G_ActorAddSkill(target, code)) {
            soul_trap_remove_possession(carrier);
            fprintf(stderr, "Soul Trap: unable to add Asou trapped state to target %.4s\n",
                    (cstring_t)&target->class_id);
            return false;
        }
        target->soul_trapped_ability_added = true;
    }
    target->soul_trap_carrier = carrier;
    target->soul_trap_carrier_spawn_time = carrier->spawn_time;
    target->soul_trap_next = carrier->soul_trap_head;
    target->soul_trap_next_spawn_time = carrier->soul_trap_head_spawn_time;
    carrier->soul_trap_head = target;
    carrier->soul_trap_head_spawn_time = target->spawn_time;
    target->soul_trap_viewer = target->s.player;
    G_AddUnitForcedVisibility(carrier, target->soul_trap_viewer);
    target->aiflags |= AI_SOUL_TRAPPED;
    S_SpellCancelChannel(target);
    G_ClearUnitOrderQueue(target);
    target->goalentity = target->combatentity = target->secondarygoal = NULL;
    if (target->stand) target->stand(target);
    target->s.renderfx |= RF_HIDDEN;
    target->svflags |= SVF_NOCLIENT;
    target->s.flags |= EF_NOT_SELECTABLE;
    if (target->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    if (G_UnitIsStructure(target)) CM_BakeStaticObstacles();
    gi.UnlinkEntity(target);
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *client = game.clients + i;
        if (!client->connected || !(target->selected & (1u << client->ps.number))) continue;
        G_DeselectEntity(client, target);
        G_SyncClientSelection(client);
    }
    target->selected = 0;
    G_InvalidateUnitShortcutsForUnit(target);
    return true;
}

/* AIso uses normal data-driven target filters; the target and carrier retain
 * identity while Asou owns release on death, explicit removal, or disposal. */
BZ_ABILITY_PROC(CAbilitySoulTrap) {
    if (!call || !call->item) return false;
    switch (msg) {
    case A_VALIDATE:
        return call->target && call->target->type == SPELL_TARGET_UNIT &&
            call->target->entity && call->target->entity->inuse &&
            !M_IsDead(call->target->entity) && !(call->target->entity->aiflags & AI_SOUL_TRAPPED);
    case A_EXECUTE:
        if (!call->target || call->target->type != SPELL_TARGET_UNIT ||
            !soul_trap_capture(ent, call->target->entity)) return false;
        if (call->source_item && call->source_item->inuse &&
            call->source_item->spawn_time == call->source_item_spawn_time &&
            call->source_item->item->carrier == ent) {
            call->source_item->item->soul_target = call->target->entity;
            call->source_item->item->soul_target_spawn_time = call->target->entity->spawn_time;
        }
        return true;
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}

static bool soul_trap_item_linked(edict_t const *carrier, edict_t const *item) {
    edict_t const *target;
    if (!carrier || !G_IsItem(item) || !item->item->soul_target) return false;
    target = item->item->soul_target;
    return target->inuse && target->spawn_time == item->item->soul_target_spawn_time &&
        target->soul_trap_item == item && target->soul_trap_item_spawn_time == item->spawn_time &&
        soul_trap_valid_link(carrier, target);
}

static void soul_trap_finalize_consumed_item(edict_t *item) {
    edict_t *carrier, *target, *filled;
    uint32_t carrier_spawn, target_spawn;
    int32_t slot;
    if (!item || !item->item->soul_target) return;
    target = item->item->soul_target;
    target_spawn = item->item->soul_target_spawn_time;
    carrier = item->item->pending_use_carrier;
    carrier_spawn = item->item->pending_use_carrier_spawn_time;
    item->item->soul_target = NULL;
    item->item->soul_target_spawn_time = 0;
    if (!target->inuse || target->spawn_time != target_spawn || !(target->aiflags & AI_SOUL_TRAPPED) ||
        !carrier || !carrier->inuse || carrier->spawn_time != carrier_spawn || M_IsDead(carrier)) return;
    slot = item->item->pending_use_slot;
    filled = slot >= 0 && slot < (int32_t)G_InventoryCapacity(carrier) ? carrier->inventory[slot] : NULL;
    if (filled && filled->class_id == ID_FILLED_SOUL && !filled->item->soul_target) {
        filled->item->soul_target = target;
        filled->item->soul_target_spawn_time = target_spawn;
        target->soul_trap_item = filled;
        target->soul_trap_item_spawn_time = filled->spawn_time;
        filled->item->droppable_set = true;
        filled->item->droppable = false;
        return;
    }
    if (!G_ItemData(ID_FILLED_SOUL)->file) {
        fprintf(stderr, "Soul Trap: ItemData row for filled item 'soul' is unresolved\n");
        return;
    }
    filled = SP_SpawnAtLocationNoBirth(ID_FILLED_SOUL, carrier->s.player, &carrier->s.origin2);
    if (!filled) return;
    filled->item->droppable_set = true;
    filled->item->droppable = false;
    slot = item->item->pending_use_slot;
    if (slot < 0 || slot >= (int32_t)G_InventoryCapacity(carrier) || carrier->inventory[slot])
        slot = G_FindFreeInventorySlot(carrier);
    if (slot >= 0 && G_AddItemToSlotInternal(carrier, filled, (uint32_t)slot, false)) {
        filled->item->soul_target = target;
        filled->item->soul_target_spawn_time = target_spawn;
        target->soul_trap_item = filled;
        target->soul_trap_item_spawn_time = filled->spawn_time;
    } else {
        fprintf(stderr, "Soul Trap: could not place filled Soul in carrier inventory\n");
        G_RemoveItem(filled);
    }
}

BZ_ABILITY_PROC(CAbilitySoulTrapped) {
    switch (msg) {
    case A_ITEM_PREVENT_DROP:
        return call && call->source_item && soul_trap_item_linked(ent, call->source_item);
    case A_ITEM_FINALIZE:
        if (call && call->source_item && call->source_item->inuse &&
            call->source_item->spawn_time == call->source_item_spawn_time &&
            call->source_item->item->pending_use_carrier == ent)
            soul_trap_finalize_consumed_item(call->source_item);
        return true;
    case A_ITEM_SCRIPT_REMOVE: {
        edict_t *item = call ? call->source_item : NULL;
        if (!soul_trap_item_linked(ent, item) || item->item->carrier != ent ||
            item->item->inventory_slot < 0 || item->item->inventory_slot >= MAX_INVENTORY ||
            ent->inventory[item->item->inventory_slot] != item) return false;
        return G_DetachItemAtScripted(ent, (uint32_t)item->item->inventory_slot);
    }
    case A_ITEM_SCRIPT_REATTACH: {
        edict_t *item = call ? call->source_item : NULL;
        int32_t slot;
        if (!soul_trap_item_linked(ent, item) || item->item->carrier != ent ||
            item->item->inventory_slot != -1 || item->item->in_world) return false;
        slot = G_FindFreeInventorySlot(ent);
        return slot >= 0 && G_ReattachItemAtScripted(ent, item, (uint32_t)slot);
    }
    case A_DEATH:
        if (ent && ent->soul_trap_head) soul_trap_release_carried(ent, &ent->s.origin2, true);
        if (ent && (ent->aiflags & AI_SOUL_TRAPPED)) soul_trap_release_target(ent, NULL, false);
        return true;
    case A_DISABLE:
        if (ent && ent->soul_trap_head) soul_trap_release_carried(ent, &ent->s.origin2, true);
        if (ent && (ent->aiflags & AI_SOUL_TRAPPED)) soul_trap_release_target(ent, &ent->s.origin2, true);
        return true;
    case A_UNIT_REMOVE:
        if (ent && ent->soul_trap_head) soul_trap_release_carried(ent, &ent->s.origin2, true);
        if (ent && (ent->aiflags & AI_SOUL_TRAPPED)) {
            ent->soul_trapped_ability_added = false;
            soul_trap_release_target(ent, NULL, false);
        }
        return true;
    default:
        return CAbilityPassive(ent, msg, call);
    }
}

BZ_ITEM_PROC(AbilityItemHeal) {
    edict_t *target = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, ID_ITEM_HEAL);
    float amount = S_SpellData(code, 1, 1);

    if (!S_SpellIsAliveTarget(target) || amount <= 0 || target->health.value >= target->health.max_value) {
        return false;
    }
    S_SpellHeal(target, amount);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}


static bool AbilityItemInvis_ItemUse(edict_t *clent) {
    edict_t *target;
    uint32_t code;
    abilityLevel_t const *row;
    cstring_t buff;
    float duration;
    bool has_status_slot;

    if (!clent || !clent->client) return false;
    code = S_SpellCurrentCode(clent, MAKEFOURCC('A','I','v','i'));
    row = G_AbilityLevel(code, 1);
    buff = row ? row->buffID : NULL;
    target = G_GetMainSelectedUnit(clent->client);
    duration = S_SpellHeroDuration(code, 1, target);
    has_status_slot = buff && strlen(buff) == 4 && unit_findstatus(target, *((uint32_t const *)buff));

    if (!S_SpellIsAliveTarget(target) || duration <= 0.0f || !buff || strlen(buff) != 4)
        return false;
    if (!has_status_slot) {
        FOR_LOOP(i, MAX_UNIT_STATUSES)
            if (!target->abilstatus[i].level) { has_status_slot = true; break; }
    }
    if (!has_status_slot) return false;

    target->s.renderfx |= RF_HIDDEN;
    {
        heroabilitystatus_t *status = S_SpellApplyTimedStatus(target, buff, 1, duration);
        if (status) status->data = code;
        if (status) return true;
    }
    if (!S_UnitHasTemporaryInvisibility(target, NULL)) {
        fprintf(stderr, "WC3 invisibility item: authored status %.4s missing after use on unit %u\n",
                buff,
                target->s.number);
        target->s.renderfx &= ~RF_HIDDEN;
    }
    return false;
}

BZ_ABILITY_PROC(CAbilityItemInvis) {
    switch (msg) {
    case A_ITEM_USE:
        return AbilityItemInvis_ItemUse(call && call->client ? call->client : ent);
    case A_STATUS_REMOVE:
        if (ent && call && call->status.slot &&
            !S_UnitHasTemporaryInvisibility(ent, call->status.slot))
            ent->s.renderfx &= ~RF_HIDDEN;
        return true;
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}

BZ_ITEM_PROC(AbilityItemManaRestore) {
    edict_t *target = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, ID_ITEM_MANA);
    float amount = S_SpellData(code, 1, 1);

    if (!target || amount <= 0 || target->mana.value >= target->mana.max_value) {
        return false;
    }
    target->mana.value = MIN(target->mana.max_value, target->mana.value + amount);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

BZ_ITEM_PROC(AbilityMaxLifeMod) {
    edict_t *target = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, ID_ITEM_LIFE_GAIN);
    float amount = S_SpellData(code, 1, 1);

    if (!target || amount <= 0) {
        return false;
    }
    target->health.max_value += amount;
    G_AddHealth(target, amount);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

/* WarSmash: CAbilityItemPermanentStatGain.checkBeforeQueue
 * Permanently adds to hero base stats, consumes the item. */
BZ_ITEM_PROC(AbilityStrengthMod) {
    edict_t *target = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, 0);
    float str = S_SpellData(code, 1, 3);
    float agi = S_SpellData(code, 1, 1);
    float intel = S_SpellData(code, 1, 2);

    if (!target || !G_UnitIsHero(target)) {
        return false;
    }
    target->hero.str += (uint32_t)str;
    target->hero.agi += (uint32_t)agi;
    target->hero.intel += (uint32_t)intel;
    G_RecomputeHeroStats(target);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

/* WarSmash: CAbilityItemExperienceGain — grants XP. */
BZ_ITEM_PROC(AbilityExperienceMod) {
    edict_t *target = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, ID_ITEM_XP_GAIN);
    uint32_t amount = (uint32_t)S_SpellData(code, 1, 1);

    if (!target || !G_UnitIsHero(target) || amount == 0) {
        return false;
    }
    G_HeroSetXP(target, target->hero.xp + amount);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

/* WarSmash: CAbilityItemLevelGain — grants hero level. */
BZ_ITEM_PROC(AbilityLevelMod) {
    edict_t *target = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, ID_ITEM_LEVEL_GAIN);
    uint32_t levels = (uint32_t)S_SpellData(code, 1, 1);

    if (!target || !G_UnitIsHero(target) || levels == 0) {
        return false;
    }
    uint32_t target_level = MIN(target->hero.level + levels, G_MaxHeroLevel());
    uint32_t target_xp = G_HeroXPForLevel(target_level);
    if (target_xp <= target->hero.xp) {
        return false;
    }
    G_HeroSetXP(target, target_xp);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

/* WarSmash: CAbilityItemFigurineSummon — summons a unit. */
BZ_ITEM_PROC(AbilityFigurineSkeleton) {
    edict_t *target = G_GetMainSelectedUnit(clent->client);
    uint32_t code = S_SpellCurrentCode(clent, ID_ITEM_FIGURINE);
    uint32_t unit_id = S_SpellUnitId(code, 1);

    if (!target || !unit_id) {
        return false;
    }
    edict_t *summon = SP_SpawnAtLocation(unit_id, target->s.player, &target->s.origin2);
    if (!summon) {
        return false;
    }
    G_ActivateUnitFood(summon);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, summon, NULL, true);
    return true;
}

/* Item-use recipient: a client's selection, the acquiring unit, or the item carrier. */
static edict_t *item_use_caster(edict_t *ent, abilityCall_t const *call) {
    if (ent && ent->client) return G_GetMainSelectedUnit(ent->client);
    if (ent && ent->inuse && ent->targtype != TARG_ITEM) return ent;
    if (call && call->source_item && G_IsItem(call->source_item)) return call->source_item->item->carrier;
    return NULL;
}

/* Item Temporary Speed Bonus / Scroll of Haste / Rune Speed AOE.
 * The authored ability supplies duration, area, target mask and BuffID.  WC3
 * treats the active speed status as maximum movement speed rather than as a
 * normal multiplicative haste bonus.  Like Area Healing, a use with valid data
 * succeeds even when no unit qualifies. */
static bool item_speed_apply(edict_t *caster, uint32_t code, bool area_effect) {
    abilityLevel_t const *row = G_AbilityLevel(code, 1);
    cstring_t buff = row ? row->buffID : NULL;

    if (!caster || !buff || strlen(buff) < 4) return false;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellAllowsAreaTarget(code, caster, target) &&
                  (area_effect ? Vector2_distance(&target->s.origin2, &caster->s.origin2) <= row->area : target == caster)) {
        float const duration = S_SpellHeroDuration(code, 1, target);
        heroabilitystatus_t *status = duration > 0.0f ? S_SpellApplyTimedTargetStatus(target, code, 1, buff, duration) : NULL;
        if (status) status->data = code;
    }
    return true;
}

/* Item Area Healing (AIha): use the acquiring unit as the AoE origin, including
 * when activated by a world powerup without an inventory slot.  All recipients
 * use the authored ability target mask and heal independently.  Full-health
 * recipients do not prevent a rune from being consumed. */
BZ_ABILITY_PROC(CAbilityItemHealAoe) {
    edict_t *caster;
    abilityLevel_t const *row;
    float amount, area;
    uint32_t code;

    if (msg != A_ITEM_USE || !call || !call->item) return false;
    caster = item_use_caster(ent, call);
    code = call->item->code;
    row = G_AbilityLevel(code, 1);
    if (!caster || !row) return false;
    amount = S_SpellData(code, 1, 1); /* DataA / Hit Points Gained */
    area = row->area;
    if (!(amount > 0.0f) || !(area >= 0.0f)) return false;

    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) &&
                 S_SpellAllowsAreaTarget(code, caster, target) &&
                 S_SpellIsFriend(caster, target) &&
                 Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area) {
        if (target->health.value >= target->health.max_value) continue;
        S_SpellHeal(target, amount);
        G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    }
    return true;
}

/* TFT Mana Runes (AImr / APmr / APmg): restore the authored DataA
 * amount to each eligible friendly unit within the authored Area. Use the
 * actual picker as origin; full-mana groups still consume the powerup. */
BZ_ABILITY_PROC(CAbilityItemManaAoe) {
    edict_t *caster;
    abilityLevel_t const *row;
    uint32_t code;
    float amount, area;

    if (msg != A_ITEM_USE || !call || !call->item) return false;
    caster = item_use_caster(ent, call);
    code = call->item->code;
    row = G_AbilityLevel(code, 1);
    if (!caster || !row) return false;
    amount = S_SpellData(code, 1, 1); /* DataA / Mana Gained */
    area = row->area;
    if (!(amount > 0.0f) || !(area >= 0.0f)) return false;

    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) &&
                 S_SpellAllowsAreaTarget(code, caster, target) &&
                 S_SpellIsFriend(caster, target) &&
                 Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area) {
        if (target->mana.value >= target->mana.max_value) continue;
        target->mana.value = MIN(target->mana.max_value, target->mana.value + amount);
        G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    }
    return true;
}

/* Chest of Gold / Gold Coins. DataA is the authored gold grant; a powerup
 * executes on its actual picker rather than on a local client's selection.
 * Resource pickups grant their full amount and do not pass through the
 * worker-harvesting upkeep tax calculation. */
BZ_ABILITY_PROC(CAbilityItemGold) {
    edict_t *caster;
    gameClient_t *owner;
    float amount;
    uint32_t balance, credited;

    if (msg != A_ITEM_USE || !call || !call->item) return false;
    caster = item_use_caster(ent, call);
    if (!caster || caster->s.player >= MAX_PLAYERS) return false;
    owner = G_GetPlayerClientByNumber(caster->s.player);
    if (!owner) return false;
    amount = S_SpellData(call->item->code, 1, 1); /* DataA / Gold Given */
    if (!(amount > 0.0f)) return false;
    balance = owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    credited = (uint32_t)MIN((double)amount, (double)(USHRT_MAX - balance));
    owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = (uint16_t)(balance + credited);
    if (credited) G_ResourceGainEvent(caster, PLAYERSTATE_RESOURCE_GOLD, (int32_t)credited);
    return true;
}

/* Bundle of Lumber (AIlu): DataA is a direct resource change, not
 * harvested income, so upkeep does not reduce it. Custom maps can author
 * negative lumber values; clamp both ends of the player resource range. */
BZ_ABILITY_PROC(CAbilityItemLumber) {
    edict_t *caster;
    gameClient_t *owner;
    float amount;
    uint32_t balance, result;

    if (msg != A_ITEM_USE || !call || !call->item) return false;
    caster = item_use_caster(ent, call);
    if (!caster || caster->s.player >= MAX_PLAYERS) return false;
    owner = G_GetPlayerClientByNumber(caster->s.player);
    if (!owner) return false;
    amount = S_SpellData(call->item->code, 1, 1); /* DataA / Lumber Given */
    if (isnan(amount)) return false;
    balance = owner->ps.stats[PLAYERSTATE_RESOURCE_LUMBER];
    result = (uint32_t)MAX(0.0, MIN((double)USHRT_MAX, (double)balance + (double)amount));
    owner->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = (uint16_t)result;
    if (result > balance) G_ResourceGainEvent(caster, PLAYERSTATE_RESOURCE_LUMBER, (int32_t)(result - balance));
    return true;
}

BZ_ABILITY_PROC(CAbilityItemSpeed) {
    if (msg != A_ITEM_USE || !call || !call->item) return false;
    return item_speed_apply(item_use_caster(ent, call), call->item->code, false);
}

BZ_ABILITY_PROC(CAbilityItemSpeedAoe) {
    if (msg != A_ITEM_USE || !call || !call->item) return false;
    return item_speed_apply(item_use_caster(ent, call), call->item->code, true);
}

bool S_ItemSpeedActive(edict_t const *unit) {
    if (!unit) return false;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t const *status = unit->abilstatus + i;
        ability_t const *ability = status->level && status->data ? S_AbilityItem(status->data).ability : NULL;
        if (ability && (ability->proc == CAbilityItemSpeed || ability->proc == CAbilityItemSpeedAoe)) return true;
    }
    return false;
}

/* Quake's itemlist decides what a touch picks up; here the AB_POWERUP row
 * flag decides which item abilities are consumed on pickup. */
abilityitem_t S_ItemPowerup(edict_t const *unit, edict_t const *item) {
    cstring_t abilities = unit && G_IsItem(item) && item->data.ItemData && item->data.ItemData->powerup &&
        item->data.ItemData->usable && G_InventoryCanUseItems(unit) ? G_ItemAbilityList(item) : NULL;
    if (!abilities) return MAKE(abilityitem_t, 0);
    PARSE_LIST(abilities, ability_name, parse_segment) {
        abilityitem_t const powerup = S_AbilityItem(FS_SLKKey(ability_name));
        if (powerup.ability && powerup.ability->flags & AB_POWERUP) return powerup;
    }
    return MAKE(abilityitem_t, 0);
}

/* Scroll of Protection / item defense AOE (AIda). Warcraft data carries the
 * defense amount in DataA, radius in Area, duration in Dur/HeroDur and the
 * visible status rawcode in BuffID. Keep the item itself as a thin ability
 * carrier: the ability data decides the actual numbers. */
BZ_ITEM_PROC(AbilityItemDefenseAoe) {
    edict_t *caster = clent && clent->client ? G_GetMainSelectedUnit(clent->client) : NULL;
    uint32_t code = S_SpellCurrentCode(clent, ID_ITEM_DEFENSE_AOE);
    uint32_t level = 1;
    float bonus = S_SpellData(code, level, 1);
    float area = S_SpellNumber(code, ABILITY_NUMBER_AREA, level);
    cstring_t buff = S_SpellBuffId(code, level);
    uint32_t affected = 0;

    if (!caster || bonus <= 0.0f || area < 0.0f || !buff || strlen(buff) < 4) {
        return false;
    }

#define ITEM_DEFENSE_AOE_TARGET(t) \
    ((t)->inuse && S_SpellIsAliveTarget(t) && S_SpellIsFriend(caster, (t)) && \
     S_SpellAllowsTarget(code, caster, (t)) && \
     Vector2_distance(&(t)->s.origin2, &caster->s.origin2) <= area)

    FILTER_EDICTS(target, ITEM_DEFENSE_AOE_TARGET(target)) {
        float duration = S_SpellHeroDuration(code, level, target);
        S_SpellApplyTimedTargetStatus(target, code, level, buff, duration);
        affected++;
    }
#undef ITEM_DEFENSE_AOE_TARGET

    return affected != 0;
}

/* Warsmash itemSimple.json: AIct (itemchangetimeofday) reads DataA/DataB as
 * hour/minute and Dur as the false-time lifetime.  The false clock is a
 * simulation override, not a renderer-only tint, so all day/night consumers
 * see the same temporary time. */
BZ_ITEM_PROC(AbilityItemChangeTOD) {
    edict_t *caster = clent && clent->client ? G_GetMainSelectedUnit(clent->client) : NULL;
    uint32_t code = S_SpellCurrentCode(clent, ID_ITEM_CHANGE_TIME);
    int32_t hour = (int32_t)S_SpellData(code, 1, 1);
    int32_t minute = (int32_t)S_SpellData(code, 1, 2);
    float duration = S_SpellDuration(code, 1, false);

    if (!caster) {
        return false;
    }
    G_SetFalseTimeOfDay(hour, minute, duration);
    return true;
}

/* Stock Build Tiny abilities share the ordinary point-targeted item pipeline.
 * Unlike worker construction, these items do not charge the building cost or
 * install a builder.  The successful A_EXECUTE result lets the inventory
 * caller consume the item only after a building has been created. */
uint32_t S_TinyStructureUnitId(edict_t const *caster, uint32_t code, uint32_t level) {
    uint32_t id = S_SpellUnitId(code, level);
    bool overridden = G_AbilityHasUnitIdOverride(code);
    /* Only the stock AIbg endpoint is race-dependent. The stock AbilityData
     * row names the Human Town Hall; expand only that authored endpoint by
     * owner race. Explicit W3A UnitID overrides remain exact.
     * AIbl and every other Tiny Structure use their authored UnitID directly. */
    if (code == MAKEFOURCC('A','I','b','g') && caster &&
        !overridden &&
        id == MAKEFOURCC('h','t','o','w')) {
        gameClient_t const *owner = G_GetPlayerClientByNumber(caster->s.player);
        if (owner) switch (owner->ps.race) {
        case kPlayerRaceHuman: id = MAKEFOURCC('h','t','o','w'); break;
        case kPlayerRaceUndead: id = MAKEFOURCC('u','n','p','l'); break;
        case kPlayerRaceNightElf: id = MAKEFOURCC('e','t','o','l'); break;
        case kPlayerRaceOrc: id = MAKEFOURCC('o','g','r','e'); break;
        default: break;
        }
    }
    return id;
}

BZ_ABILITY_PROC(CAbilityTinyStructure) {
    uint32_t code = call && call->item ? call->item->code : 0;
    uint32_t level = code ? MAX(1u, S_SpellLevel(ent, code)) : 0;
    uint32_t unit_id = code ? S_TinyStructureUnitId(ent, code, level) : 0;
    vec2_t snapped;
    if (msg != A_VALIDATE && msg != A_EXECUTE)
        return CAbilitySimpleSpell(ent, msg, call);
    if (!ent || !call || !call->target ||
        call->target->type != SPELL_TARGET_POINT || !unit_id ||
        !G_UnitIsBuilding(unit_id) || M_IsDead(ent) ||
        G_EvaluateBuildPlacement(ent, unit_id, &call->target->point, &snapped) != PLACE_OK)
        return false;
    if (msg == A_VALIDATE) return true;
    edict_t *building = SP_SpawnAtLocation(unit_id, ent->s.player, &snapped);
    if (!building) return false;
    /* A displacement failure is a failed cast: do not consume the item or
     * leave a newly spawned, blocking structure behind. */
    if (!G_DisplaceBuildOccupants(ent, building)) {
        G_FreeEdict(building);
        return false;
    }
    if (!G_StartTinyConstruction(ent, building, S_SpellDuration(code, level, false))) {
        G_FreeEdict(building);
        return false;
    }
    G_PublishEvent(building, EVENT_PLAYER_UNIT_CONSTRUCT_START);
    if (building->inuse && building->construction &&
        S_SpellDuration(code, level, false) <= 0.0f)
        G_CompleteConstruction(building);
    return true;
}
