#ifndef sv_quest_h
#define sv_quest_h

#include "../common/shared.h"

svQuestEntry_t *SV_QuestFind(svQuestEntry_t *log, uint32_t count, uint32_t quest_id);
bool SV_QuestAdd(svQuestEntry_t *log, uint32_t *count, uint32_t max_log, uint32_t quest_id);

#endif
