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

#define WOW_QUEST_MAX_OBJECTIVE_TEXT 4
#define WOW_QUEST_MAX_REWARD_ITEMS   2

typedef struct {
    DWORD quest_id;
    LPCSTR title;
    LPCSTR description;
    LPCSTR objectives_text;
    LPCSTR reward_text;
    DWORD reward_xp;
    DWORD reward_gold;
    DWORD reward_items[WOW_QUEST_MAX_REWARD_ITEMS];
    DWORD prev_quest;           /* prerequisite quest ID, 0 = none */
    DWORD min_level;
} WOWQUESTDETAIL;

typedef enum {
    WOW_QUEST_NONE,
    WOW_QUEST_ACCEPTED,
    WOW_QUEST_COMPLETE,
    WOW_QUEST_REWARDED,
} wowQuestStatus_t;

typedef struct {
    DWORD quest_id;
    wowQuestStatus_t status;
} wowQuestState_t;

#define WOW_MAX_QUEST_LOG 16

typedef const WOWQUESTGIVER *LPCWOWQUESTGIVER;
typedef const WOWQUESTOBJECTIVE *LPCWOWQUESTOBJECTIVE;
typedef const WOWQUESTDETAIL *LPCWOWQUESTDETAIL;

#define WOW_QUEST_OBJECTIVE_ANCHOR 0x51504F49

DWORD Wow_QuestGiverCount(void);
LPCWOWQUESTGIVER Wow_QuestGiver(DWORD index);
DWORD Wow_QuestObjectiveCount(void);
LPCWOWQUESTOBJECTIVE Wow_QuestObjective(DWORD index);
LPCWOWQUESTDETAIL Wow_QuestDetail(DWORD quest_id);

#endif
