#include "wow_quest_data.h"

static const WOWQUESTGIVER wow_quest_givers[] = {
    { 5, 288, 4325, { -10731.4f, 337.138f, 38.4686f }, 0.2883f },
    { 6, 823, 2072, { -8947.64f, -132.319f, 83.7199f }, 3.33358f },
    { 7, 197, 1859, { -8902.59f, -162.606f, 82.0223f }, 2.04204f },
    { 8, 6784, 5509, { 2126.65f, 1305.98f, 53.9885f }, 2.83146f },
    { 9, 233, 1943, { -10128.7f, 1055.21f, 36.4333f }, 2.44346f },
    { 11, 963, 3279, { -9662.75f, 694.321f, 36.9357f }, 3.28122f },
    { 12, 234, 1690, { -10508.8f, 1045.23f, 60.7013f }, 1.95477f },
    { 16, 255, 3324, { -9918.6f, 39.3611f, 32.6858f }, 3.14159f },
    { 17, 1470, 1834, { -5395.86f, -3016.18f, 327.663f }, 0.436332f },
    { 19, 382, 3454, { -9284.03f, -2298.16f, 67.5772f }, 1.13446f },
    { 22, 235, 1691, { -10112.1f, 1042.1f, 37.5542f }, 2.42601f },
    { 33, 196, 3251, { -8869.22f, -163.237f, 80.9719f }, 0.959931f },
    { 34, 342, 1743, { -9245.81f, -2045.33f, 77.1861f }, 5.23599f },
    { 35, 240, 1985, { -9465.52f, 74.0069f, 56.7785f }, 4.59022f },
    { 36, 238, 1692, { -9853.05f, 919.542f, 30.4614f }, 4.7822f },
    { 37, 261, 1984, { -9610.23f, -1032.05f, 41.3058f }, 3.14159f },
    { 40, 241, 3254, { -9496.32f, 72.8264f, 56.598f }, 6.23099f },
};

static const WOWQUESTOBJECTIVE wow_quest_objectives[] = {
    { 5, { -10499.0f, -1158.0f } }, { 6, { -9056.0f, -461.0f } },
    { 7, { -8797.0f, -259.0f } }, { 8, { 2270.0f, 245.0f } },
    { 9, { -9934.0f, 1166.0f } }, { 11, { -9983.0f, 549.0f } },
    { 12, { -10179.0f, 1878.0f } }, { 13, { -10727.0f, 1659.0f } },
    { 14, { -11258.0f, 1506.0f } }, { 15, { -8782.0f, -274.0f } },
    { 16, { -9983.0f, 549.0f } }, { 17, { -6172.0f, -3030.0f } },
    { 18, { -9052.0f, -458.0f } }, { 19, { -9435.0f, -3079.0f } },
    { 20, { -9816.0f, -3260.0f } }, { 21, { -8561.0f, -218.0f } },
    { 22, { -10448.0f, 872.0f } }, { 30, { 846.0f, 2208.0f } },
    { 33, { -8837.0f, -305.0f } }, { 34, { -9289.0f, -1919.0f } },
    { 35, { -9610.0f, -1032.0f } }, { 36, { -10112.0f, 1042.0f } },
    { 37, { -9336.0f, -986.0f } }, { 38, { -10586.0f, 816.0f } },
    { 39, { -9466.0f, 74.0f } }, { 40, { -9466.0f, 74.0f } },
};

static const WOWQUESTDETAIL wow_quest_details[] = {
    { 5, "Jitters: Growling Gut" }, { 6, "Bounty on Garrick Padfoot" },
    { 7, "Kobold Camp Cleanup" }, { 8, "A Rogue's Deal" },
    { 9, "The Killing Fields" }, { 11, "Riverpaw Gnoll Bounty" },
    { 12, "The People's Militia" }, { 13, "The People's Militia" },
    { 14, "The People's Militia" }, { 15, "Investigate Echo Ridge" },
    { 16, "Give Gerard a Drink" }, { 17, "Uldaman Reagent Run" },
    { 18, "Brotherhood of Thieves" }, { 19, "Tharil'zun" },
    { 20, "Blackrock Menace" }, { 21, "Skirmish at Echo Ridge" },
    { 22, "Goretusk Liver Pie" }, { 33, "Wolves Across the Border" },
    { 34, "An Unwelcome Guest" }, { 35, "Further Concerns" },
    { 36, "Westfall Stew" }, { 37, "Find the Lost Guards" },
    { 38, "Westfall Stew" }, { 39, "Deliver Thomas' Report" },
    { 40, "A Fishy Peril" },
};

DWORD Wow_QuestGiverCount(void) { return sizeof(wow_quest_givers) / sizeof(wow_quest_givers[0]); }
LPCWOWQUESTGIVER Wow_QuestGiver(DWORD index) {
    return index < Wow_QuestGiverCount() ? &wow_quest_givers[index] : NULL;
}
DWORD Wow_QuestObjectiveCount(void) {
    return sizeof(wow_quest_objectives) / sizeof(wow_quest_objectives[0]);
}
LPCWOWQUESTOBJECTIVE Wow_QuestObjective(DWORD index) {
    return index < Wow_QuestObjectiveCount() ? &wow_quest_objectives[index] : NULL;
}
LPCWOWQUESTDETAIL Wow_QuestDetail(DWORD quest_id) {
    FOR_LOOP(i, sizeof(wow_quest_details) / sizeof(wow_quest_details[0]))
        if (wow_quest_details[i].quest_id == quest_id) return &wow_quest_details[i];
    return NULL;
}
