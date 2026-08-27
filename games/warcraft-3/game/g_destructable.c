#include "g_local.h"

#define DESTRUCTABLE_DROP_RADIUS 32.0f // world units; separates multiple drops around one destroyed object
#define NO_RANDOM_ITEM_TABLE ((DWORD)-1)

static void G_ApplyDestructableAlivePathing(LPEDICT ent) {
    ent->pathtex = ent->destructable.placement_solid
        ? ent->destructable.alive_pathtex
        : NULL;
    ent->collision = ent->destructable.placement_solid
        ? ent->destructable.alive_collision
        : 0.0f;
    ent->destructable.pathing_active = ent->destructable.placement_solid &&
        (ent->pathtex || ent->collision > 0.0f);
}

static void G_ApplyDestructableDeathPathing(LPEDICT ent) {
    ent->pathtex = ent->destructable.placement_solid
        ? ent->destructable.death_pathtex
        : NULL;
    ent->collision = 0.0f;
    ent->destructable.pathing_active = ent->destructable.placement_solid &&
        ent->pathtex != NULL;
}

BOOL G_IsDestructable(LPCEDICT ent) {
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
    return level.mapinfo && DESTRUCTABLE_FILE(ent->class_id) != NULL;
}

BOOL G_DestructableIsAttackable(LPCEDICT ent) {
    return G_IsDestructable(ent) && !ent->destructable.dead &&
        ent->health.value > 0.0f && ent->targtype != TARG_NONE &&
        !(ent->s.renderfx & RF_HIDDEN) &&
        !(ent->s.flags & EF_NOT_SELECTABLE);
}

/* Resolve one 0..99 roll against cumulative percentages. Any unused remainder
 * intentionally represents no item, matching the map editor's item-set data. */
DWORD G_SelectDropItem(droppableItem_t const *entries, DWORD count, DWORD roll) {
    DWORD threshold = 0;

    if (!entries || roll >= 100) {
        return 0;
    }
    FOR_LOOP(i, count) {
        LONG chance = entries[i].chanceToDrop;

        if (chance <= 0) {
            continue;
        }
        threshold += MIN((DWORD)chance, 100 - threshold);
        if (roll < threshold) {
            return entries[i].itemID;
        }
        if (threshold == 100) {
            break;
        }
    }
    return 0;
}

DWORD G_SelectRandomTableItem(mapRandomItem_t const *entries, DWORD count, DWORD roll) {
    DWORD threshold = 0;

    if (!entries || roll >= 100) {
        return 0;
    }
    FOR_LOOP(i, count) {
        DWORD chance = entries[i].chance;

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

mapRandomItemTable_t const *G_FindRandomItemTable(DWORD table_number) {
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

static BOOL G_IsEncodedRandomItem(DWORD item_id) {
    BYTE const *code = (BYTE const *)&item_id;

    return code[0] == 'Y' && code[1] == 'Y' && code[2] == 'I';
}

static void G_QueueDestructableDrop(DWORD item_id,
                                    DWORD *selected,
                                    DWORD *selected_count) {
    LPCSTR item_file;

    if (!item_id) {
        return;
    }
    if (G_IsEncodedRandomItem(item_id)) {
        fprintf(stderr, "G_SpawnDestructableLoot: unsupported encoded random item 0x%08x\n", item_id);
        return;
    }
    item_file = ITEM_FILE(item_id);
    if (!item_file || !*item_file) {
        fprintf(stderr, "G_SpawnDestructableLoot: invalid item ID 0x%08x\n", item_id);
        return;
    }
    selected[(*selected_count)++] = item_id;
}

/* Spawn each selected inline or map-table drop as a normal neutral-passive
 * world item.
 * Marking first makes the operation safe against callbacks or repeated kills. */
void G_SpawnDestructableLoot(LPEDICT ent) {
    mapRandomItemTable_t const *table;
    DWORD *selected;
    DWORD selected_count = 0;
    DWORD max_selected;

    if (!G_IsDestructable(ent) || !ent->destructable.dead || ent->destructable.loot_processed) {
        return;
    }
    ent->destructable.loot_processed = true;
    table = G_FindRandomItemTable(ent->destructable.item_table);
    if (ent->destructable.item_table != NO_RANDOM_ITEM_TABLE && !table) {
        fprintf(stderr, "G_SpawnDestructableLoot: missing random item table %u\n",
                (unsigned)ent->destructable.item_table);
    }
    max_selected = ARRAY_COUNT(ent->destructable.drop_sets) +
        (table && table->sets ? table->num_sets : 0);
    if (!max_selected) {
        return;
    }

    selected = gi.MemAlloc(sizeof(*selected) * max_selected);
    FOR_EACH_ARRAY(droppableItemSet_t const, set, ent->destructable.drop_sets) {
        DWORD item_id;

        if (!set->droppableItems || set->num_droppableItems <= 0) {
            continue;
        }
        item_id = G_SelectDropItem(set->droppableItems, (DWORD)set->num_droppableItems, (DWORD)(rand() % 100));
        G_QueueDestructableDrop(item_id, selected, &selected_count);
    }
    if (table && table->sets) {
        FOR_LOOP(i, table->num_sets) {
            mapRandomItemSet_t const *set = &table->sets[i];
            DWORD item_id;

            if (!set->items || !set->num_items) {
                continue;
            }
            item_id = G_SelectRandomTableItem(set->items, set->num_items,
                                              (DWORD)(rand() % 100));
            G_QueueDestructableDrop(item_id, selected, &selected_count);
        }
    }

    FOR_LOOP(i, selected_count) {
        FLOAT angle = selected_count > 1 ? 2.0f * M_PI * (FLOAT)i / (FLOAT)selected_count : 0.0f;
        FLOAT radius = selected_count > 1 ? DESTRUCTABLE_DROP_RADIUS : 0.0f;
        VECTOR2 point = {
            ent->s.origin.x + cosf(angle) * radius,
            ent->s.origin.y + sinf(angle) * radius,
        };

        SP_SpawnAtLocation(selected[i], PLAYER_NEUTRAL_PASSIVE, &point);
    }
    gi.MemFree(selected);
}

static BOOL G_EnterDestructableDeathState(LPEDICT ent,
                                          LPEDICT killer,
                                          BOOL publish_event,
                                          BOOL rebuild_pathing) {
    void (*callback)(LPEDICT, LPEDICT);

    if (!G_IsDestructable(ent) || ent->destructable.dead) {
        return false;
    }

    ent->destructable.dead = true;
    ent->health.value = 0.0f;
    ent->svflags |= SVF_DEADMONSTER;
    ent->s.flags |= EF_NOT_SELECTABLE;
    unit_leavecombat(ent);
    G_ApplyDestructableDeathPathing(ent);
    if (ent->s.flags & EF_FOW_BLOCKER) {
        G_FowMarkBlockersDirty();
    }
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

void G_InitializeDestructablePlacement(LPEDICT ent, LPCDOODAD placement) {
    FLOAT life_fraction;
    BOOL visible;

    if (!ent || !placement || !ent->destructable.initialized) {
        return;
    }

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
    G_ApplyDestructableAlivePathing(ent);

    life_fraction = (FLOAT)placement->treeLife / 100.0f;
    ent->health.value = MAX(0.0f, ent->health.max_value * life_fraction);
    if (ent->health.value <= 0.0f) {
        G_EnterDestructableDeathState(ent, NULL, false, false);
    }
}

BOOL G_KillDestructable(LPEDICT ent, LPEDICT killer) {
    return G_EnterDestructableDeathState(ent, killer, true, true);
}

BOOL G_SetDestructableDeadState(LPEDICT ent, BOOL process_death) {
    return G_EnterDestructableDeathState(ent, NULL, process_death, true);
}

BOOL G_RemoveDestructable(LPEDICT ent) {
    if (!G_IsDestructable(ent)) {
        return false;
    }
    unit_leavecombat(ent);
    G_FreeEdict(ent);
    CM_BakeStaticObstacles();
    return true;
}

BOOL G_RestoreDestructable(LPEDICT ent, FLOAT life, BOOL birth) {
    FLOAT restored_life;

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
    if (!(ent->s.renderfx & RF_HIDDEN)) {
        ent->s.flags &= ~EF_NOT_SELECTABLE;
    }
    G_ApplyDestructableAlivePathing(ent);
    G_DestructableStartAliveAnimation(ent, birth);
    if (ent->s.flags & EF_FOW_BLOCKER) {
        G_FowMarkBlockersDirty();
    }
    CM_BakeStaticObstacles();
    return true;
}

BOOL G_SetDestructableLife(LPEDICT ent, FLOAT life) {
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

BOOL G_DestructableApplyDamage(LPEDICT ent, LPEDICT attacker, FLOAT damage) {
    if (!G_IsDestructable(ent) || ent->destructable.dead ||
        ent->invulnerable || damage <= 0.0f) {
        return false;
    }

    if (damage >= ent->health.value) {
        return G_KillDestructable(ent, attacker);
    }

    ent->health.value -= damage;
    if (ent->pain) {
        ent->pain(ent);
    }
    return false;
}
