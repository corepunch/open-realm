#ifndef g_shared_h
#define g_shared_h

enum {
    CmdAttack,
    CmdAttackGround,
    CmdBuild,
    CmdBuildHuman,
    CmdBuildOrc,
    CmdBuildNightElf,
    CmdBuildUndead,
    CmdCancel,
    CmdCancelBuild,
    CmdCancelTrain,
    CmdCancelRevive,
    CmdHoldPos,
    CmdMove,
    CmdPatrol,
    CmdPurchase,
    CmdRally,
    CmdSelectSkill,
    CmdStop,
    CmdUnivAgi,
    CmdUnivInt,
    CmdUnivStr,
};

#define STR_CmdAttack "CmdAttack"
#define STR_CmdAttackGround "CmdAttackGround"
#define STR_CmdBuild "CmdBuild"
#define STR_CmdBuildHuman "CmdBuildHuman"
#define STR_CmdBuildOrc "CmdBuildOrc"
#define STR_CmdBuildNightElf "CmdBuildNightElf"
#define STR_CmdBuildUndead "CmdBuildUndead"
#define STR_CmdCancel "CmdCancel"
#define STR_CmdCancelBuild "CmdCancelBuild"
#define STR_CmdCancelTrain "CmdCancelTrain"
#define STR_CmdCancelRevive "CmdCancelRevive"
#define STR_CmdHoldPos "CmdHoldPos"
#define STR_CmdMove "CmdMove"
#define STR_CmdPatrol "CmdPatrol"
#define STR_CmdPurchase "CmdPurchase"
#define STR_CmdRally "CmdRally"
#define STR_CmdSelectSkill "CmdSelectSkill"
#define STR_CmdStop "CmdStop"
#define STR_CmdUnivAgi "CmdUnivAgi"
#define STR_CmdUnivInt "CmdUnivInt"
#define STR_CmdUnivStr "CmdUnivStr"

#define STR_CmdTrains "CmdTrains"

#define STR_ART "Art"
#define STR_UNART "Unart"
#define STR_DEFAULT "Default"
#define STR_BUTTONPOS "Buttonpos"
#define STR_UNBUTTONPOS "UnButtonpos"
#define STR_HOTKEY "Hotkey"
#define STR_UNHOTKEY "Unhotkey"
#define STR_TIP "Tip"
#define STR_UNTIP "Untip"
#define STR_UBERTIP "Ubertip"
#define STR_UNUBERTIP "Unubertip"
#define STR_PLACEMENT_MODEL "PlacementModel"
#define STR_PLACEMENT_TEXTURE "PlacementTexture"
#define STR_TARGET_ART "TargetArt"

#define STR_HUMAN "human"
#define STR_ORC "orc"
#define STR_UNDEAD "undead"
#define STR_NIGHTELF "nightelf"
#define STR_NAGA "naga"
#define STR_DEMON "demon"
#define STR_CREEPS "creeps"
#define STR_CRITTERS "critters"
#define STR_OTHER "other"
#define STR_COMMONER "commoner"

typedef enum {
    RACE_UNKNOWN,
    RACE_HUMAN,
    RACE_ORC,
    RACE_UNDEAD,
    RACE_NIGHTELF,
    RACE_NAGA,
    RACE_DEMON,
    RACE_CREEPS,
    RACE_CRITTERS,
    RACE_OTHER,
    RACE_COMMONER,
} unitRace_t;

typedef struct {
    cstring_t name;
    unitRace_t race;
    int32_t jass_value;
} wc3RaceName_t;

static wc3RaceName_t const wc3_race_names[] = {
    { STR_HUMAN, RACE_HUMAN, 1 }, { STR_ORC, RACE_ORC, 2 },
    { STR_UNDEAD, RACE_UNDEAD, 3 }, { STR_NIGHTELF, RACE_NIGHTELF, 4 },
    { STR_DEMON, RACE_DEMON, 5 }, { STR_CREEPS, RACE_CREEPS, 8 },
    { STR_OTHER, RACE_OTHER, 7 }, { STR_CRITTERS, RACE_CRITTERS, 10 },
    { STR_COMMONER, RACE_COMMONER, 9 }, { STR_NAGA, RACE_NAGA, 11 },
};

/* Resolve authored WC3 race names through one shared table used by game/UI code. */
static inline unitRace_t WC3_RaceFromString(cstring_t name) {
    if (!name) return RACE_UNKNOWN;
    FOR_LOOP(i, sizeof(wc3_race_names) / sizeof(*wc3_race_names))
        if (!strcasecmp(name, wc3_race_names[i].name)) return wc3_race_names[i].race;
    return RACE_UNKNOWN;
}

/* UnitData race names and the JASS race enum use related but distinct values. */
static inline int32_t WC3_JassRaceFromString(cstring_t name) {
    if (!name) return 0;
    FOR_LOOP(i, sizeof(wc3_race_names) / sizeof(*wc3_race_names))
        if (!strcasecmp(name, wc3_race_names[i].name)) return wc3_race_names[i].jass_value;
    return 0;
}

#define WC3_GOLD_MINE_MIN_DISTANCE 512.0f

#define BZ_WC3_UNIT_GOLD_MINE          MAKEFOURCC('n','g','o','l')
#define BZ_WC3_UNIT_HAUNTED_GOLD_MINE MAKEFOURCC('u','g','o','l')
#define BZ_WC3_UNIT_TOWN_HALL          MAKEFOURCC('h','t','o','w')
#define BZ_WC3_UNIT_CRYPT              MAKEFOURCC('u','s','e','p')
#define BZ_WC3_UNIT_BARRACKS           MAKEFOURCC('h','b','a','r')
#define BZ_WC3_UNIT_PEASANT            MAKEFOURCC('h','p','e','a')
#define BZ_WC3_UNIT_FOOTMAN            MAKEFOURCC('h','f','o','o')

#endif
