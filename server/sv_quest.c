#include "server.h"
#include "sv_quest.h"

svQuestEntry_t *SV_QuestFind(svQuestEntry_t *log, DWORD count, DWORD quest_id) {
    DWORD i;
    for (i = 0; i < count; i++)
        if (log[i].quest_id == quest_id) return &log[i];
    return NULL;
}

BOOL SV_QuestAdd(svQuestEntry_t *log, DWORD *count, DWORD max_log, DWORD quest_id) {
    if (*count >= max_log) return false;
    if (SV_QuestFind(log, *count, quest_id)) return false;
    log[*count].quest_id = quest_id;
    log[*count].status = SV_QUEST_ACTIVE;
    (*count)++;
    return true;
}
