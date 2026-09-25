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
    if (carrier && carrier->inuse && carrier->spawn_time == carrier_spawn)
        soul_trap_remove_possession(carrier);
}

static void soul_trap_release_target(edict_t *target, vector2_t const *position, bool restore_world) {
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
        item->item.soul_target == target && item->item.soul_target_spawn_time == target->spawn_time) {
        item->item.soul_target = NULL;
        item->item.soul_target_spawn_time = 0;
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
        if (G_UnitIsBuilding(target->class_id)) CM_BakeStaticObstacles();
    }
    if (remove_asou && G_ActorHasSkill(target, "Asou"))
        G_ActorRemoveSkill(target, ID_SOUL_POSSESSION);
}

static void soul_trap_release_carried(edict_t *carrier, vector2_t const *position, bool restore_world) {
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
    target->aiflags |= AI_SOUL_TRAPPED;
    S_SpellCancelChannel(target);
    G_ClearUnitOrderQueue(target);
    target->goalentity = target->combatentity = target->secondarygoal = NULL;
    if (target->stand) target->stand(target);
    target->s.renderfx |= RF_HIDDEN;
    target->svflags |= SVF_NOCLIENT;
    target->s.flags |= EF_NOT_SELECTABLE;
    if (target->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    if (G_UnitIsBuilding(target->class_id)) CM_BakeStaticObstacles();
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
            call->source_item->item.carrier == ent) {
            call->source_item->item.soul_target = call->target->entity;
            call->source_item->item.soul_target_spawn_time = call->target->entity->spawn_time;
        }
        return true;
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}

bool S_SoulTrapRevealsCarrier(edict_t const *carrier, uint32_t viewer) {
    edict_t *target;
    uint32_t spawn_time, guard = 0;
    if (!carrier || viewer >= MAX_PLAYERS) return false;
    target = carrier->soul_trap_head;
    spawn_time = carrier->soul_trap_head_spawn_time;
    while (target && guard++ < globals.max_edicts) {
        edict_t *next;
        uint32_t next_spawn;
        if (!soul_trap_valid_link(carrier, target) || target->spawn_time != spawn_time) break;
        if (target->s.player == viewer || G_FowPlayersShareVision(viewer, target->s.player)) return true;
        next = target->soul_trap_next;
        next_spawn = target->soul_trap_next_spawn_time;
        target = next;
        spawn_time = next_spawn;
    }
    return false;
}

void S_SoulTrapFinalizeConsumedItem(edict_t *item) {
    edict_t *carrier, *target, *filled;
    uint32_t carrier_spawn, target_spawn;
    int32_t slot;
    if (!item || !item->item.soul_target) return;
    target = item->item.soul_target;
    target_spawn = item->item.soul_target_spawn_time;
    carrier = item->item.pending_use_carrier;
    carrier_spawn = item->item.pending_use_carrier_spawn_time;
    item->item.soul_target = NULL;
    item->item.soul_target_spawn_time = 0;
    if (!target->inuse || target->spawn_time != target_spawn || !(target->aiflags & AI_SOUL_TRAPPED) ||
        !carrier || !carrier->inuse || carrier->spawn_time != carrier_spawn || M_IsDead(carrier)) return;
    FOR_LOOP(i, G_InventoryCapacity(carrier)) {
        filled = carrier->inventory[i];
        if (!filled || filled->class_id != ID_FILLED_SOUL) continue;
        if (filled->item.soul_target &&
            (filled->item.soul_target != target || filled->item.soul_target_spawn_time != target_spawn)) continue;
        filled->item.soul_target = target;
        filled->item.soul_target_spawn_time = target_spawn;
        target->soul_trap_item = filled;
        target->soul_trap_item_spawn_time = filled->spawn_time;
        filled->item.droppable_set = true;
        filled->item.droppable = false;
        return;
    }
    if (!G_ItemData(ID_FILLED_SOUL)->file) {
        fprintf(stderr, "Soul Trap: ItemData row for filled item 'soul' is unresolved\n");
        return;
    }
    filled = SP_SpawnAtLocationNoBirth(ID_FILLED_SOUL, carrier->s.player, &carrier->s.origin2);
    if (!filled) return;
    filled->item.droppable_set = true;
    filled->item.droppable = false;
    slot = item->item.pending_use_slot;
    if (slot < 0 || slot >= (int32_t)G_InventoryCapacity(carrier) || carrier->inventory[slot])
        slot = G_FindFreeInventorySlot(carrier);
    if (slot >= 0 && G_AddItemToSlotInternal(carrier, filled, (uint32_t)slot, false)) {
        filled->item.soul_target = target;
        filled->item.soul_target_spawn_time = target_spawn;
        target->soul_trap_item = filled;
        target->soul_trap_item_spawn_time = filled->spawn_time;
    } else {
        fprintf(stderr, "Soul Trap: could not place filled Soul in carrier inventory\n");
        G_RemoveItem(filled);
    }
}

BZ_ABILITY_PROC(CAbilitySoulTrapped) {
    switch (msg) {
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
    cstring_t buff = G_AbilityLevel(code, level)->buffID;
    uint32_t affected = 0;

    if (!caster || bonus <= 0.0f || area < 0.0f || !buff || strlen(buff) < 4) {
        return false;
    }

#define ITEM_DEFENSE_AOE_TARGET(t) \
    ((t)->inuse && S_SpellIsAliveTarget(t) && S_SpellIsFriend(caster, (t)) && \
     S_SpellAllowsTarget(code, caster, (t)) && \
     Vector2_distance(&(t)->s.origin2, &caster->s.origin2) <= area)

    FILTER_EDICTS(target, ITEM_DEFENSE_AOE_TARGET(target)) {
        float duration = S_SpellDuration(code, level, G_UnitIsHero(target));
        unit_addtimedstatus(target, buff, level, duration);
        G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
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
