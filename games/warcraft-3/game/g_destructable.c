#include "g_local.h"

#define DESTRUCTABLE_DROP_RADIUS 32.0f // world units; separates multiple drops around one destroyed object
#define NO_RANDOM_ITEM_TABLE ((uint32_t)-1) // table index; war3map.doo sentinel meaning no random-item table
#define RANDOM_ITEM_PREFIX_MASK 0x00ffffff // bits; compare the YYI prefix while ignoring its encoded selector byte

static void G_ApplyDestructableAlivePathing(edict_t *ent) {
    ent->pathtex = ent->destructable.placement_solid
        ? ent->destructable.alive_pathtex
        : NULL;
    ent->collision = ent->destructable.placement_solid
        ? ent->destructable.alive_collision
        : 0.0f;
    ent->destructable.pathing_active = ent->destructable.placement_solid &&
        (ent->pathtex || ent->collision > 0.0f);
    if (ent->data.DestructableData && ent->data.DestructableData->walkable &&
        ent->destructable.placement_solid && !ent->destructable.dead)
        ent->s.flags |= EF_GROUND_SURFACE;
    else
        ent->s.flags &= ~EF_GROUND_SURFACE;
}

static void G_ApplyDestructableDeathPathing(edict_t *ent) {
    ent->pathtex = ent->destructable.placement_solid
        ? ent->destructable.death_pathtex
        : NULL;
    ent->collision = 0.0f;
    ent->destructable.pathing_active = ent->destructable.placement_solid &&
        ent->pathtex != NULL;
    ent->s.flags &= ~EF_GROUND_SURFACE;
}

/*
 * Activate a preplaced war3map.doo placeholder when generated war3map.j
 * creates the corresponding destructable through CreateDestructable().
 */
void G_ActivateScriptedDestructable(edict_t *ent,
                                    float x,
                                    float y,
                                    float z,
                                    float facing,
                                    float scale,
                                    uint32_t variation) {
    if (!ent || !G_IsDestructable(ent)) {
        return;
    }

    ent->variation = variation;

    ent->s.origin.x = x;
    ent->s.origin.y = y;
    ent->s.origin.z = z;
    ent->s.angle = facing;
    ent->s.scale = scale;

    ent->destructable.dead = false;
    ent->destructable.loot_processed = false;

    /*
     * CreateDestructable creates the normal active form, even when the
     * war3map.doo entry used as its placeholder had flags=0.
     */
    ent->destructable.placement_solid = true;

    ent->svflags &= ~SVF_DEADMONSTER;

    ent->s.renderfx &= ~RF_HIDDEN;
    ent->s.renderfx &= ~RF_NO_SHADOW;
    ent->s.flags &= ~EF_NOT_SELECTABLE;

    ent->health.value = ent->health.max_value;

    G_ApplyDestructableAlivePathing(ent);
    G_DestructableStartAliveAnimation(ent, false);
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();

    gi.LinkEntity(ent);
}

bool G_IsDestructable(edict_t const *ent) {
    if (!ent || !ent->inuse || !ent->class_id) {
        return false;
    }
    if (ent->destructable.initialized) {
        return true;
    }
    /* Spawned units are never destructables.  Besides avoiding an object-data
     * lookup on every ordinary combat hit, this prevents a rawcode collision
     * between unit and destructable tables from changing the damage path. */
    if (ent->svflags & SVF_MONSTER) {
        return false;
    }
    return level.mapinfo && ent->data.DestructableData && ent->data.DestructableData->file != NULL;
}

bool G_DestructableIsAttackable(edict_t const *ent) {
    return G_IsDestructable(ent) && !ent->destructable.dead &&
        ent->health.value > 0.0f && ent->targtype != TARG_NONE &&
        !(ent->s.renderfx & RF_HIDDEN) &&
        !(ent->s.flags & EF_NOT_SELECTABLE);
}

bool G_DestructableIsWalkable(edict_t const *ent) {
    return G_IsDestructable(ent) && ent->data.DestructableData->walkable &&
        ent->destructable.placement_solid && !ent->destructable.dead;
}

/* Warcraft target flags are shared with ordinary unit weapon targeting;
 * G_TargetFlagForType() owns the TARGTYPE -> common.j bit conversion. */
bool G_DestructableCanBeAttackedBy(edict_t const *attacker, edict_t const *target) {
    uint32_t flag;

    if (!attacker || !G_DestructableIsAttackable(target) ||
        (attacker->attack1.type == ATK_NONE && attacker->attack2.type == ATK_NONE)) {
        return false;
    }
    /* Retail lets the explicit Attack command cut down trees even though
     * standard UnitWeapons.slk melee target lists usually omit "tree".
     * Smart handling is still separate and workers keep Harvest precedence. */
    if (target->targtype == TARG_TREE)
        return (attacker->attack1.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 0)) ||
               (attacker->attack2.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 1));
    flag = G_TargetFlagForType(target->targtype);
    return flag && ((attacker->attack1.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 0) && (attacker->attack1.targetsAllowed & flag)) ||
                    (attacker->attack2.type != ATK_NONE && S_UnitAttackSlotEnabled(attacker, 1) && (attacker->attack2.targetsAllowed & flag)));
}

bool G_DestructableAcceptsSmartAttack(edict_t const *attacker, edict_t const *target) {
    /* Retail Smart/right-click treats attackable walls like gates as attack
     * targets. Bridges retain walk-to behavior unless explicitly attackable. */
    return G_DestructableCanBeAttackedBy(attacker, target) &&
        (target->targtype == TARG_DEBRIS || target->targtype == TARG_WALL);
}

/* Resolve one 0..99 roll against cumulative percentages. Any unused remainder
 * intentionally represents no item, matching the map editor's item-set data. */
uint32_t G_SelectDropItem(droppableItem_t const *entries, uint32_t count, uint32_t roll) {
    uint32_t threshold = 0;

    if (!entries || roll >= 100) {
        return 0;
    }
    FOR_LOOP(i, count) {
        int32_t chance = entries[i].chanceToDrop;

        if (chance <= 0) {
            continue;
        }
        threshold += MIN((uint32_t)chance, 100 - threshold);
        if (roll < threshold) {
            return entries[i].itemID;
        }
        if (threshold == 100) {
            break;
        }
    }
    return 0;
}

uint32_t G_SelectRandomTableItem(mapRandomItem_t const *entries, uint32_t count, uint32_t roll) {
    uint32_t threshold = 0;

    if (!entries || roll >= 100) {
        return 0;
    }
    FOR_LOOP(i, count) {
        uint32_t chance = entries[i].chance;

        threshold += MIN(chance, 100 - threshold);
        if (roll < threshold) {
            return entries[i].itemID;
        }
        if (threshold == 100) {
            break;
        }
    }
    return 0;
}

mapRandomItemTable_t const *G_FindRandomItemTable(uint32_t table_number) {
    if (!level.mapinfo || !level.mapinfo->randomItems ||
        table_number == NO_RANDOM_ITEM_TABLE) {
        return NULL;
    }
    FOR_LOOP(i, level.mapinfo->num_randomItems) {
        if (level.mapinfo->randomItems[i].tableNumber == table_number) {
            return &level.mapinfo->randomItems[i];
        }
    }
    return NULL;
}

static bool G_IsEncodedRandomItem(uint32_t item_id) {
    return (item_id & RANDOM_ITEM_PREFIX_MASK) == (MAKEFOURCC('Y', 'Y', 'I', 0) & RANDOM_ITEM_PREFIX_MASK);
}

static void G_QueueDestructableDrop(uint32_t item_id,
                                    uint32_t *selected,
                                    uint32_t *selected_count) {
    cstring_t item_file;

    if (!item_id) return;
    if (G_IsEncodedRandomItem(item_id)) {
        /* TODO: YYI* entries require item-class/level expansion that is not
         * represented by the parsed map table yet. */
        fprintf(stderr, "G_SpawnDestructableLoot: unsupported encoded random item 0x%08x\n", item_id);
        return;
    }
    item_file = G_ItemData(item_id)->file;
    if (!item_file || !*item_file) {
        fprintf(stderr, "G_SpawnDestructableLoot: invalid item ID 0x%08x\n", item_id);
        return;
    }
    selected[(*selected_count)++] = item_id;
}

/* Spawn each selected inline or map-table drop as a normal neutral-passive
 * world item.
 * Marking first makes the operation safe against callbacks or repeated kills. */
void G_SpawnDestructableLoot(edict_t *ent) {
    mapRandomItemTable_t const *table;
    uint32_t *selected;
    uint32_t selected_count = 0;
    uint32_t max_selected;

    if (!ent || !G_IsDestructable(ent) || !ent->destructable.dead || ent->destructable.loot_processed) return;
    ent->destructable.loot_processed = true;
    table = G_FindRandomItemTable(ent->destructable.item_table);
    if (ent->destructable.item_table != NO_RANDOM_ITEM_TABLE && !table) {
        fprintf(stderr, "G_SpawnDestructableLoot: missing random item table %u\n",
                (unsigned)ent->destructable.item_table);
    }
    max_selected = ARRAY_COUNT(ent->destructable.drop_sets) +
        (table && table->sets ? table->num_sets : 0);
    if (!max_selected) return;

    selected = gi.MemAlloc(sizeof(*selected) * max_selected);
    FOR_LOOP(i, ARRAY_COUNT(ent->destructable.drop_sets)) {
        droppableItemSet_t const *set = ent->destructable.drop_sets + i;
        uint32_t item_id, roll;

        if (!set->droppableItems || set->num_droppableItems <= 0) continue;
        roll = (uint32_t)(rand() % 100);
        item_id = G_SelectDropItem(set->droppableItems, (uint32_t)set->num_droppableItems, roll);
        G_QueueDestructableDrop(item_id, selected, &selected_count);
    }
    if (table && table->sets) {
        FOR_LOOP(i, table->num_sets) {
            mapRandomItemSet_t const *set = &table->sets[i];
            uint32_t item_id, roll;

            if (!set->items || !set->num_items) continue;
            roll = (uint32_t)(rand() % 100);
            item_id = G_SelectRandomTableItem(set->items, set->num_items, roll);
            G_QueueDestructableDrop(item_id, selected, &selected_count);
        }
    }

    FOR_LOOP(i, selected_count) {
        float angle = selected_count > 1 ? 2.0f * M_PI * (float)i / (float)selected_count : 0.0f;
        float radius = selected_count > 1 ? DESTRUCTABLE_DROP_RADIUS : 0.0f;
        vec2_t point = {
            ent->s.origin.x + cosf(angle) * radius,
            ent->s.origin.y + sinf(angle) * radius,
        };

        edict_t *item = SP_SpawnAtLocation(selected[i], PLAYER_NEUTRAL_PASSIVE, &point);

        if (!item) fprintf(stderr, "G_SpawnDestructableLoot: failed to spawn item 0x%08x\n", selected[i]);
    }
    gi.MemFree(selected);
}

static bool G_EnterDestructableDeathState(edict_t *ent,
                                          edict_t *killer,
                                          bool publish_event,
                                          bool rebuild_pathing) {
    void (*callback)(edict_t *, edict_t *);

    if (!G_IsDestructable(ent) || ent->destructable.dead) return false;

    ent->destructable.dead = true;
    ent->health.value = 0.0f;
    ent->svflags |= SVF_DEADMONSTER;
    ent->s.flags |= EF_NOT_SELECTABLE;

    /* Dead destructable remains keep their model but no longer cast the
     * alive destructable's blob/splat shadow. */
    ent->s.renderfx |= RF_NO_SHADOW;

    unit_leavecombat(ent);
    G_ApplyDestructableDeathPathing(ent);
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    G_DestructableStartDeathAnimation(ent);
    if (rebuild_pathing) {
        CM_BakeStaticObstacles();
    }
    if (publish_event) {
        G_SpawnDestructableLoot(ent);
        G_PublishEventWithSource(ent, EVENT_UNIT_DEATH, killer);
    } else {
        /* Initially dead and CreateDeadDestructable instances did not die in
         * gameplay, so they must not expose deferred loot. Restoration starts
         * a fresh lifecycle and clears this guard. */
        ent->destructable.loot_processed = true;
    }

    /* The lifecycle is complete before an optional compatibility callback is
     * invoked. tree_die is only the legacy entry point back into this function. */
    callback = ent->die;
    if (publish_event && callback && callback != tree_die) {
        callback(ent, killer);
    }
    return true;
}

void G_InitializeDestructablePlacement(edict_t *ent, doodad_t const *placement) {
    float life_fraction;
    bool visible;

    if (!ent || !placement || !ent->destructable.initialized) {
        return;
    }

    ent->destructable.map_placed = true;
    ent->destructable.script_bound = false;

    ent->destructable.editor_id = placement->unitID;
    ent->destructable.item_table = placement->droppedItemSetPtr;
    ent->destructable.drop_sets = placement->droppableItemSets;
    ARRAY_COUNT(ent->destructable.drop_sets) = placement->num_droppedItemSets;
    ent->destructable.loot_processed = false;
    ent->destructable.placement_solid = (placement->flags & 2) != 0;
    visible = placement->flags != 0;
    if (visible) {
        ent->s.renderfx &= ~RF_HIDDEN;
        ent->s.flags &= ~EF_NOT_SELECTABLE;
    } else {
        ent->s.renderfx |= RF_HIDDEN;
        ent->s.flags |= EF_NOT_SELECTABLE;
    }

    ent->destructable.dead = false;
    ent->svflags &= ~SVF_DEADMONSTER;
    ent->s.renderfx &= ~RF_NO_SHADOW;
    G_ApplyDestructableAlivePathing(ent);

    life_fraction = (float)placement->treeLife / 100.0f;
    ent->health.value = MAX(0.0f, ent->health.max_value * life_fraction);
    if (ent->health.value <= 0.0f) {
        G_EnterDestructableDeathState(ent, NULL, false, false);
    }
}

bool G_KillDestructable(edict_t *ent, edict_t *killer) {
    return G_EnterDestructableDeathState(ent, killer, true, true);
}

bool G_SetDestructableDeadState(edict_t *ent, bool process_death) {
    return G_EnterDestructableDeathState(ent, NULL, process_death, true);
}

bool G_RemoveDestructable(edict_t *ent) {
    if (!G_IsDestructable(ent)) {
        return false;
    }
    unit_leavecombat(ent);
    G_FreeEdict(ent);
    CM_BakeStaticObstacles();
    return true;
}

bool G_RestoreDestructable(edict_t *ent, float life, bool birth) {
    float restored_life;

    if (!G_IsDestructable(ent)) {
        return false;
    }
    restored_life = MAX(0.0f, MIN(life, ent->health.max_value));
    if (restored_life <= 0.0f) {
        return false;
    }
    ent->health.value = restored_life;
    if (!ent->destructable.dead) {
        return true;
    }

    ent->destructable.dead = false;
    ent->destructable.loot_processed = false;
    ent->svflags &= ~SVF_DEADMONSTER;
    ent->aiflags &= ~AI_HOLD_FRAME;

    /* Restored destructables regain their normal shadow. */
    ent->s.renderfx &= ~RF_NO_SHADOW;

    if (!(ent->s.renderfx & RF_HIDDEN)) {
        ent->s.flags &= ~EF_NOT_SELECTABLE;
    }
    G_ApplyDestructableAlivePathing(ent);
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    G_DestructableStartAliveAnimation(ent, birth);
    CM_BakeStaticObstacles();
    return true;
}

bool G_SetDestructableLife(edict_t *ent, float life) {
    if (!G_IsDestructable(ent)) {
        return false;
    }
    if (life <= 0.0f) {
        if (ent->destructable.dead) {
            ent->health.value = 0.0f;
            return true;
        }
        return G_KillDestructable(ent, NULL);
    }
    if (ent->destructable.dead) {
        return G_RestoreDestructable(ent, life, false);
    }
    ent->health.value = MAX(0.0f, MIN(life, ent->health.max_value));
    return true;
}

bool G_DestructableApplyDamage(edict_t *ent, edict_t *attacker, float damage) {
    if (!G_IsDestructable(ent) || ent->destructable.dead || ent->invulnerable || damage <= 0.0f) return false;

    if (damage >= ent->health.value) {
        return G_KillDestructable(ent, attacker);
    }

    ent->health.value -= damage;
    if (ent->pain) {
        ent->pain(ent);
    }
    return false;
}
