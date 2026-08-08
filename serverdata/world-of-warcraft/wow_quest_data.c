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
    {
        5, "Jitters: Growling Gut",
        "Jitters the goblin wants me to collect 5 pieces of Goretusk Snout meat from the boars outside of Stormwind.",
        "Collect 5 Goretusk Snouts for Jitters.",
        "Thanks for the snouts! Here's a little something for your trouble.",
        250, 35, {0, 0}, 0, 1
    }, {
        6, "Bounty on Garrick Padfoot",
        "Deputy Willem has posted a bounty on Garrick Padfoot, the leader of the Riverpaw gnolls. Bring me his paw as proof.",
        "Kill Garrick Padfoot and bring his paw to Deputy Willem.",
        "You did it! Garrick Padfoot won't be troubling us anymore. Here's your reward.",
        340, 50, {0, 0}, 0, 1
    }, {
        7, "Kobold Camp Cleanup",
        "Marshal McBride wants me to clear out the kobold vermin from the Echo Ridge Mine. Kill 10 kobold vermin.",
        "Kill 10 Kobold Vermin for Marshal McBride.",
        "The kobolds have been driven back. The mine is safe again, thanks to you.",
        340, 50, {0, 0}, 0, 1
    }, {
        8, "A Rogue's Deal",
        "A rogue in the Den wants me to collect some supplies from the dwarven outpost.",
        "Deliver the rogue's package to the dwarven outpost.",
        "Well done. The deal is sealed. Take this for your trouble.",
        180, 25, {0, 0}, 0, 2
    }, {
        9, "The Killing Fields",
        "Farmer Saldean needs help protecting his fields from the mechanical harvesters running wild.",
        "Destroy 8 Harvest Watchers in the fields west of Sentinel Hill.",
        "The fields are safe for now. Thank you, adventurer.",
        560, 75, {0, 0}, 0, 5
    }, {
        11, "Riverpaw Gnoll Bounty",
        "Marshal Dughan has offered a bounty on Riverpaw gnolls. Bring me their painted gnoll armbands.",
        "Collect 8 Painted Gnoll Armbands from Riverpaw gnolls.",
        "These armbands prove your service to Stormwind. Well fought.",
        450, 60, {0, 0}, 0, 4
    }, {
        12, "The People's Militia",
        "Gryan Stoutmantle is recruiting able-bodied adventurers for the People's Militia. Speak with him in Westfall.",
        "Report to Gryan Stoutmantle at Sentinel Hill in Westfall.",
        "Welcome to the People's Militia. We need every hand we can get.",
        170, 15, {0, 0}, 0, 3
    }, {
        13, "The People's Militia",
        "Gryan Stoutmantle wants me to help the people of Westfall. Kill 12 Defias bandits.",
        "Kill 12 Defias bandits for the People's Militia.",
        "Good work. But the Defias are still a threat.",
        620, 80, {0, 0}, 12, 7
    }, {
        14, "The People's Militia",
        "Gryan Stoutmantle wants me to kill 15 Defias Trappers and 15 Defias Smugglers.",
        "Kill 15 Defias Trappers and 15 Defias Smugglers in Westfall.",
        "Excellent! These victories will be remembered.",
        780, 100, {0, 0}, 13, 9
    }, {
        15, "Investigate Echo Ridge",
        "Marshal McBride wants me to investigate Echo Ridge Mine and report back on the kobold infestation.",
        "Investigate Echo Ridge Mine and return to Marshal McBride.",
        "So the kobolds have infested the entire mine. This is worse than I thought.",
        170, 15, {0, 0}, 0, 2
    }, {
        16, "Give Gerard a Drink",
        "A thirsty guard named Gerard needs a drink. Bring him some water from the well.",
        "Bring a water skin to Guard Gerard.",
        "Ahh, that hits the spot. Thanks friend.",
        85, 10, {0, 0}, 0, 1
    }, {
        17, "Uldaman Reagent Run",
        "A dwarven alchemist needs reagents from the Uldaman excavation. Collect 5 samples.",
        "Collect 5 dig-site reagent samples from the Uldaman excavation.",
        "These reagents are perfect! My experiments can continue.",
        1250, 250, {0, 0}, 0, 30
    }, {
        18, "Brotherhood of Thieves",
        "Marshal Gryan wants me to recover stolen goods from the Defias Brotherhood.",
        "Recover 8 Stolen Supply Sacks from Defias bandits.",
        "These supplies will be returned to their rightful owners.",
        510, 65, {0, 0}, 0, 8
    }, {
        19, "Tharil'zun",
        "Marshal Gryan has a special assignment — find and eliminate the orc warlord Tharil'zun.",
        "Kill Tharil'zun and return to Marshal Gryan at Sentinel Hill.",
        "Tharil'zun is dead! You've done Stormwind a great service.",
        875, 125, {0, 0}, 0, 12
    }, {
        20, "Blackrock Menace",
        "Marshal Gryan is concerned about the Blackrock orcs in Redridge. Investigate their presence.",
        "Investigate the Blackrock orc presence in Redridge and report back.",
        "The Blackrock orcs in Redridge? This is troubling news indeed.",
        390, 45, {0, 0}, 0, 8
    }, {
        21, "Skirmish at Echo Ridge",
        "Marshal McBride wants me to help secure Echo Ridge from the kobold threat.",
        "Help defend Echo Ridge against the kobolds.",
        "Echo Ridge is secure. For now.",
        250, 35, {0, 0}, 15, 3
    }, {
        22, "Goretusk Liver Pie",
        "Salma Saldean wants me to collect 8 Goretusk Livers for her famous pies.",
        "Collect 8 Goretusk Livers for Salma Saldean.",
        "These livers will make excellent pies! Here's your share.",
        450, 60, {0, 0}, 0, 3
    }, {
        33, "Wolves Across the Border",
        "Eagan Peltskinner wants me to thin the wolf population outside Northshire. Kill 8 wolves.",
        "Kill 8 Young Wolves for Eagan Peltskinner.",
        "The wolf population is under control now. Good hunting.",
        210, 25, {0, 0}, 0, 2
    }, {
        34, "An Unwelcome Guest",
        "Deputy Feldon needs me to deal with an unwelcome guest at the vineyard.",
        "Drive off the unwelcome guest at the vineyard.",
        "The vineyard is safe again. Thank you.",
        310, 40, {0, 0}, 0, 4
    }, {
        35, "Further Concerns",
        "Marshal McBride has further concerns about the kobold threat. Kill 12 more kobolds.",
        "Kill 12 more Kobold Vermin for Marshal McBride.",
        "Excellent work! Perhaps the kobolds will think twice now.",
        410, 55, {0, 0}, 7, 3
    }, {
        36, "Westfall Stew",
        "Farmer Saldean needs ingredients for his famous Westfall Stew.",
        "Collect stew ingredients: Stringy Vulture Meat, Murloc Eyes, Okra, and Goretusk Snouts.",
        "This stew will feed the whole militia! Thank you.",
        560, 75, {0, 0}, 0, 6
    }, {
        37, "Find the Lost Guards",
        "Marshal Gryan sent guards to investigate the Jangolode Mine and they haven't returned.",
        "Locate the lost guards near the Jangolode Mine.",
        "So they're dead. At least we know what happened. This information is valuable.",
        680, 85, {0, 0}, 0, 8
    }, {
        38, "Westfall Stew",
        "Farmer Saldean needs more ingredients for a larger batch of Westfall Stew.",
        "Collect additional stew ingredients for Farmer Saldean.",
        "More stew for the lads! You're a lifesaver.",
        390, 50, {0, 0}, 36, 8
    }, {
        39, "Deliver Thomas' Report",
        "Deliver Guard Thomas' report to Marshal Dughan in Goldshire.",
        "Deliver the report to Marshal Dughan in Goldshire.",
        "Thank you for delivering this report. The information is crucial.",
        120, 15, {0, 0}, 0, 2
    }, {
        40, "A Fishy Peril",
        "Marshal Dughan has received reports of murlocs along the river. Investigate the threat.",
        "Investigate the murloc threat along the Elwynn River.",
        "Murlocs in the river! I'll mobilize the guards at once.",
        210, 25, {0, 0}, 0, 3
    },
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
