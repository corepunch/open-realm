#ifndef WOW_QUEST_DATA_H
#define WOW_QUEST_DATA_H

#include "common/shared.h"

typedef struct {
    DWORD quest_id;
    DWORD creature_entry;
    DWORD display_id;
    VECTOR3 position;
    FLOAT orientation;
} WOWQUESTGIVER;

typedef struct {
    DWORD quest_id;
    VECTOR2 position;
} WOWQUESTOBJECTIVE;

typedef const WOWQUESTGIVER *LPCWOWQUESTGIVER;
typedef const WOWQUESTOBJECTIVE *LPCWOWQUESTOBJECTIVE;

#define WOW_QUEST_OBJECTIVE_ANCHOR 0x51504F49

DWORD Wow_QuestGiverCount(void);
LPCWOWQUESTGIVER Wow_QuestGiver(DWORD index);
DWORD Wow_QuestObjectiveCount(void);
LPCWOWQUESTOBJECTIVE Wow_QuestObjective(DWORD index);

#endif
