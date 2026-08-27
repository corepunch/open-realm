#include "g_local.h"

#define DESTRUCTABLE_DROP_RADIUS 32.0f // world units; separates multiple drops around one destroyed object

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

/* Spawn each selected inline drop as a normal neutral-passive world item.
 * Marking first makes the operation safe against callbacks or repeated kills. */
void G_SpawnDestructableLoot(LPEDICT ent) {
    DWORD *selected;
    DWORD selected_count = 0;

    if (!G_IsDestructable(ent) || !ent->destructable.dead || ent->destructable.loot_processed) {
        return;
    }
    ent->destructable.loot_processed = true;
    if (IS_ARRAY_EMPTY(ent->destructable.drop_sets)) {
        return;
    }

    selected = gi.MemAlloc(sizeof(*selected) * ARRAY_COUNT(ent->destructable.drop_sets));
    FOR_EACH_ARRAY(droppableItemSet_t const, set, ent->destructable.drop_sets) {
        DWORD item_id;
        LPCSTR item_file;

        if (!set->droppableItems || set->num_droppableItems <= 0) {
            continue;
        }
        item_id = G_SelectDropItem(set->droppableItems, (DWORD)set->num_droppableItems, (DWORD)(rand() % 100));
        if (!item_id) {
            continue;
        }
        item_file = ITEM_FILE(item_id);
        if (!item_file || !*item_file) {
            fprintf(stderr, "G_SpawnDestructableLoot: invalid item ID 0x%08x\n", item_id);
            continue;
        }
        selected[selected_count++] = item_id;
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
        G_PublishEvent(ent, EVENT_UNIT_DEATH);
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
