#ifndef sv_quest_h
#define sv_quest_h

#include "../common/shared.h"

svQuestEntry_t *SV_QuestFind(svQuestEntry_t *log, DWORD count, DWORD quest_id);
BOOL SV_QuestAdd(svQuestEntry_t *log, DWORD *count, DWORD max_log, DWORD quest_id);

#endif
