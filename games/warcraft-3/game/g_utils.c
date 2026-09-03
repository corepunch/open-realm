#include "g_local.h"

void G_SetPlayerText(LPGAMECLIENT client, PLAYERTEXT index, LPCSTR text) {
    DWORD cursor;

    if (!client || index >= PLAYERTEXT_COUNT) {
        return;
    }
    cursor = ++client->playerTextCursor[index] & PLAYER_TEXT_MASK;
    snprintf(client->playerTextStorage[index][cursor],
             sizeof(client->playerTextStorage[index][cursor]),
             "%s",
             text ? text : "");
    client->ps.texts[index] = client->playerTextStorage[index][cursor];
}

void G_FreeActorSkills(LPEDICT ent) {
    if (ent->added_abilities) gi.MemFree(ent->added_abilities);
    if (ent->removed_abilities) gi.MemFree(ent->removed_abilities);
    if (ent->permanent_abilities) gi.MemFree(ent->permanent_abilities);
    ent->added_abilities = ent->removed_abilities = ent->permanent_abilities = NULL;
    ARRAY_COUNT(ent->added_abilities) = ARRAY_COUNT(ent->removed_abilities) = ARRAY_COUNT(ent->permanent_abilities) = 0;
}

void G_FreeEdict(LPEDICT ent) {
    if (!ent) return;
    /* Removed units cannot remain in JASS groups: save files require every group member to resolve to a live edict. */
    FOR_LOOP(i, level.num_groups) {
        ggroup_t *group = level.groups[i];
        if (!group) continue;
        for (DWORD k = 0; k < group->num_units;) {
            if (group->units[k] != ent) { k++; continue; }
            for (DWORD n = k + 1; n < group->num_units; n++) group->units[n - 1] = group->units[n];
            group->num_units--;
        }
    }
    G_UnregisterGroundSurface(ent);
    G_InvalidateUnitShortcutsForUnit(ent);
    G_InvalidateRallyTarget(ent);
    if (ent->revival.reviving) G_CancelHeroRevive(ent->revival.producer, ent);
    if (ent->training) G_ClearTrainingQueueFood(ent);
    else { G_CancelHeroRevives(ent); G_CancelTrainingQueue(ent, true); }
    G_ClearUnitFood(ent);
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    S_GoldMineReleaseWorker(ent);
    gi.UnlinkEntity(ent);
    G_FreeActorSkills(ent);
    memset(ent, 0, sizeof(*ent));
    ent->freetime = level.time;
}

LPEVENT G_MakeEvent(EVENTTYPE type) {
    LPEVENT evt = gi.MemAlloc(sizeof(EVENT));
    evt->type = type;
    ADD_TO_LIST(evt, level.events.handlers);
    return evt;
}

BOOL G_RegionContains(LPCREGION region, LPCVECTOR2 point) {
    FOR_LOOP(i, region->num_rects) {
        if (Box2_containsPoint(region->rects+i, point)) {
            return true;
        }
    }
    return false;
}

LPQUEST G_MakeQuest(void) {
    LPQUEST quest = gi.MemAlloc(sizeof(QUEST));
    /* CreateQuestBJ does not call QuestSetEnabled; Warcraft quests are usable
     * immediately unless a map explicitly disables them. */
    quest->enabled = true;
    PUSH_BACK(QUEST, quest, level.quests);
    return quest;
}

static void DeleteQuestItem(LPQUESTITEM questitem) {
    free(questitem->description);
}

static void DeleteQuest(LPQUEST quest) {
    DELETE_LIST(QUESTITEM, quest->items, DeleteQuestItem);
    free(quest->description);
    free(quest->title);
    free(quest->iconPath);
    gi.MemFree(quest);
}

void G_RemoveQuest(LPQUEST quest) {
    REMOVE_FROM_LIST(QUEST, quest, level.quests, DeleteQuest);
}

void G_SetPlayerAlliance(LPCPLAYER p1, LPCPLAYER p2, PLAYERALLIANCE type, BOOL value) {
    DWORD const flag = 1u << type;
    DWORD const before = level.alliances[p1->number][p2->number];

    if (value) level.alliances[p1->number][p2->number] |= flag;
    else level.alliances[p1->number][p2->number] &= ~flag;

    /* Warcraft alliance state is directional: SetPlayerAlliance(source, other, ...)
     * changes only source -> other. Consumers such as fog and shared command
     * authority already read the matrix in that direction. */
    if ((type == ALLIANCE_PASSIVE || type == ALLIANCE_SHARED_CONTROL) &&
        before != level.alliances[p1->number][p2->number]) {
        G_InvalidateAllUnitShortcuts();
    }
}

BOOL G_GetPlayerAlliance(LPCPLAYER p1, LPCPLAYER p2, PLAYERALLIANCE type) {
    return level.alliances[p1->number][p2->number] & (1 << type);
}
