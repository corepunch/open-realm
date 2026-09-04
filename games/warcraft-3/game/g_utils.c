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

void G_FreeEdict(LPEDICT ent) {
    if (!ent) return;
    /* Removed units cannot remain in JASS groups: save files require every group member to resolve to a live edict. */
    FOR_LOOP(i, level.num_groups) {
        ggroup_t *group = &level.groups[i];
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
    memset(ent, 0, sizeof(*ent));
    ent->freetime = level.time;
}

LPEVENT G_MakeEvent(EVENTTYPE type) {
    FOR_LOOP(i, MAX_EVENTS) if (!level.events.handlers[i].inuse) {
        LPEVENT evt = &level.events.handlers[i];
        memset(evt, 0, sizeof(*evt)); evt->inuse = true; evt->type = type; return evt;
    }
    fprintf(stderr, "WC3: event slot limit %u reached\n", MAX_EVENTS);
    return NULL;
}

ggroup_t *G_AllocJassGroup(void) {
    if (level.num_groups >= MAX_GROUPS) return NULL;
    ggroup_t *group = &level.groups[level.num_groups++];
    memset(group, 0, sizeof(*group)); return group;
}

LPTRIGGER G_AllocJassTrigger(void) {
    if (level.num_triggers >= MAX_TRIGGERS) return NULL;
    LPTRIGGER trigger = &level.triggers[level.num_triggers++];
    memset(trigger, 0, sizeof(*trigger)); return trigger;
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
    FOR_LOOP(i, MAX_QUESTS) if (!level.quests[i].inuse) {
    LPQUEST quest = &level.quests[i];
    memset(quest, 0, sizeof(*quest));
    /* CreateQuestBJ does not call QuestSetEnabled; Warcraft quests are usable
     * immediately unless a map explicitly disables them. */
    quest->inuse = true; quest->enabled = true;
    return quest;
    }
    fprintf(stderr, "WC3: quest slot limit %u reached\n", MAX_QUESTS);
    return NULL;
}

static void DeleteQuestItem(LPQUESTITEM questitem) {
    free(questitem->description);
    memset(questitem, 0, sizeof(*questitem));
}

static void DeleteQuest(LPQUEST quest) {
    FOR_LOOP(i, MAX_QUESTITEMS) if (quest->items[i].inuse) DeleteQuestItem(&quest->items[i]);
    free(quest->description);
    free(quest->title);
    free(quest->iconPath);
    memset(quest, 0, sizeof(*quest));
}

void G_RemoveQuest(LPQUEST quest) {
    if (quest && quest->inuse) DeleteQuest(quest);
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
