// Minimal common.j for WC3 in-engine test fixtures.
// Declares only the types and natives exercised by the test suite.
// Not a substitute for the real common.j — do not add production map content here.

// Handle subtypes.
// Units, destructables, and items must be convertible to widget because
// TriggerRegisterDeathEvent accepts Warcraft III's common widget base type.
type agent            extends handle
type widget           extends agent
type unit             extends widget
type destructable     extends widget
type item             extends widget
type player           extends agent
type quest            extends handle
type questitem        extends handle
type playergameresult extends handle
type trigger          extends handle
type event            extends handle
type triggeraction    extends handle
type playerevent      extends handle
type racepreference  extends handle
type mapcontrol      extends handle
type gametype        extends handle
type mapflag         extends handle
type placement       extends handle
type startlocprio    extends handle
type mapdensity      extends handle
type gamedifficulty  extends handle
type gamespeed       extends handle
type playerstate     extends handle
type rect            extends handle
type region          extends handle
type location        extends handle
type force           extends handle
type boolexpr        extends handle
type conditionfunc   extends boolexpr
type filterfunc      extends boolexpr

// Cinematic skip regression uses the same event and local-player guards as campaign scripts.
native ConvertPlayerEvent         takes integer i returns playerevent
native CreateTrigger              takes nothing returns trigger
native TriggerRegisterPlayerEvent takes trigger whichTrigger, player whichPlayer, playerevent whichPlayerEvent returns event
native TriggerAddAction           takes trigger whichTrigger, code actionFunc returns triggeraction
native GetLocalPlayer             takes nothing returns player
native ShowInterface              takes boolean flag, real fadeDuration returns nothing
native EnableUserControl          takes boolean b returns nothing
native ResetToGameCamera          takes real duration returns nothing
native PanCameraTo                takes real x, real y returns nothing

// Map and player configuration.
native ConvertRacePref       takes integer i returns racepreference
native ConvertMapControl     takes integer i returns mapcontrol
native ConvertGameType       takes integer i returns gametype
native ConvertMapFlag        takes integer i returns mapflag
native ConvertPlacement      takes integer i returns placement
native ConvertStartLocPrio   takes integer i returns startlocprio
native ConvertMapDensity     takes integer i returns mapdensity
native ConvertGameDifficulty takes integer i returns gamedifficulty
native ConvertGameSpeed      takes integer i returns gamespeed
native ConvertPlayerState    takes integer i returns playerstate
native SetMapName            takes string name returns nothing
native SetMapDescription     takes string description returns nothing
native SetTeams              takes integer teamcount returns nothing
native SetPlayers            takes integer playercount returns nothing
native SetStartLocPrioCount  takes integer whichStartLoc, integer prioSlotCount returns nothing
native SetStartLocPrio       takes integer whichStartLoc, integer prioSlotIndex, integer otherStartLocIndex, startlocprio priority returns nothing
native GetStartLocPrioSlot   takes integer whichStartLoc, integer prioSlotIndex returns integer
native GetStartLocPrio       takes integer whichStartLoc, integer prioSlotIndex returns startlocprio
native SetGameTypeSupported  takes gametype whichGameType, boolean value returns nothing
native IsGameTypeSupported   takes gametype whichGameType returns boolean
native SetMapFlag            takes mapflag whichMapFlag, boolean value returns nothing
native IsMapFlagSet          takes mapflag whichMapFlag returns boolean
native SetGamePlacement      takes placement whichPlacementType returns nothing
native GetGamePlacement      takes nothing returns placement
native SetGameSpeed          takes gamespeed whichSpeed returns nothing
native GetGameSpeed          takes nothing returns gamespeed
native SetGameDifficulty     takes gamedifficulty whichDifficulty returns nothing
native GetGameDifficulty     takes nothing returns gamedifficulty
native SetResourceDensity    takes mapdensity whichDensity returns nothing
native GetResourceDensity    takes nothing returns mapdensity
native SetCreatureDensity    takes mapdensity whichDensity returns nothing
native GetCreatureDensity    takes nothing returns mapdensity
native SetPlayerName         takes player whichPlayer, string name returns nothing
native GetPlayerName         takes player whichPlayer returns string
native SetPlayerRacePreference takes player whichPlayer, racepreference whichRacePreference returns nothing
native IsPlayerRacePrefSet   takes player whichPlayer, racepreference pref returns boolean
native SetPlayerRaceSelectable takes player whichPlayer, boolean value returns nothing
native GetPlayerSelectable   takes player whichPlayer returns boolean
native SetPlayerController   takes player whichPlayer, mapcontrol controlType returns nothing
native GetPlayerController   takes player whichPlayer returns mapcontrol
native SetPlayerTaxRate      takes player sourcePlayer, player otherPlayer, playerstate whichResource, integer rate returns nothing
native GetPlayerTaxRate      takes player sourcePlayer, player otherPlayer, playerstate whichResource returns integer
native SetPlayerHandicap     takes player whichPlayer, real handicap returns nothing
native GetPlayerHandicap     takes player whichPlayer returns real
native SetPlayerHandicapXP   takes player whichPlayer, real handicap returns nothing
native GetPlayerHandicapXP   takes player whichPlayer returns real
native SetPlayerOnScoreScreen takes player whichPlayer, boolean flag returns nothing
native Rect                   takes real minx, real miny, real maxx, real maxy returns rect
native CreateRegion           takes nothing returns region
native RegionAddRect          takes region whichRegion, rect r returns nothing
native RegionClearRect        takes region whichRegion, rect r returns nothing
native Location               takes real x, real y returns location
native IsPointInRegion        takes region whichRegion, real x, real y returns boolean
native IsLocationInRegion     takes region whichRegion, location whichLocation returns boolean
native CreateForce            takes nothing returns force
native ForceAddPlayer         takes force whichForce, player whichPlayer returns nothing
native ForceRemovePlayer      takes force whichForce, player whichPlayer returns nothing
native ForceClear             takes force whichForce returns nothing
native ForceEnumPlayers       takes force whichForce, boolexpr filter returns nothing
native ForceEnumPlayersCounted takes force whichForce, boolexpr filter, integer countLimit returns nothing
native ForceEnumAllies        takes force whichForce, player whichPlayer, boolexpr filter returns nothing
native ForceEnumEnemies       takes force whichForce, player whichPlayer, boolexpr filter returns nothing
native ForForce               takes force whichForce, code callback returns nothing
native GetFilterPlayer        takes nothing returns player
native GetEnumPlayer          takes nothing returns player
native Condition              takes code func returns conditionfunc
native IsPlayerInForce        takes player whichPlayer, force whichForce returns boolean
native GetPlayerId            takes player whichPlayer returns integer

// Win conditions.
native ConvertPlayerGameResult  takes integer i returns playergameresult
native RemovePlayer             takes player whichPlayer, playergameresult gameResult returns nothing
native Player                   takes integer number returns player

// Quest management.
native CreateQuest               takes nothing returns quest
native DestroyQuest              takes quest whichQuest returns nothing
native QuestSetTitle             takes quest whichQuest, string title returns nothing
native QuestSetDescription       takes quest whichQuest, string description returns nothing
native QuestSetIconPath          takes quest whichQuest, string iconPath returns nothing
native QuestSetRequired          takes quest whichQuest, boolean required returns nothing
native QuestSetCompleted         takes quest whichQuest, boolean completed returns nothing
native QuestSetDiscovered        takes quest whichQuest, boolean discovered returns nothing
native QuestSetFailed            takes quest whichQuest, boolean failed returns nothing
native QuestSetEnabled           takes quest whichQuest, boolean enabled returns nothing
native IsQuestRequired           takes quest whichQuest returns boolean
native IsQuestCompleted          takes quest whichQuest returns boolean
native IsQuestDiscovered         takes quest whichQuest returns boolean
native IsQuestFailed             takes quest whichQuest returns boolean
native IsQuestEnabled            takes quest whichQuest returns boolean
native QuestCreateItem           takes quest whichQuest returns questitem
native QuestItemSetDescription   takes questitem whichQuestItem, string description returns nothing
native QuestItemSetCompleted     takes questitem whichQuestItem, boolean completed returns nothing
native IsQuestItemCompleted      takes questitem whichQuestItem returns boolean

// In-engine test assertion hooks (api_test.h).
native BJassAssert  takes boolean cond, string msg returns nothing
native BJassError   takes string msg returns nothing

// Player game result constants — must live in a globals block (top-level
// "constant <type>" is not valid; only "constant native" is top-level).
globals
    constant playerevent EVENT_PLAYER_END_CINEMATIC = ConvertPlayerEvent(17)
    constant playergameresult PLAYER_GAME_RESULT_VICTORY = ConvertPlayerGameResult(0)
    constant playergameresult PLAYER_GAME_RESULT_DEFEAT  = ConvertPlayerGameResult(1)
    constant playergameresult PLAYER_GAME_RESULT_TIE     = ConvertPlayerGameResult(2)
    constant playergameresult PLAYER_GAME_RESULT_NEUTRAL = ConvertPlayerGameResult(3)
    constant racepreference RACE_PREF_HUMAN = ConvertRacePref(1)
    constant racepreference RACE_PREF_ORC = ConvertRacePref(2)
    constant racepreference RACE_PREF_RANDOM = ConvertRacePref(32)
    constant mapcontrol MAP_CONTROL_COMPUTER = ConvertMapControl(1)
    constant gametype GAME_TYPE_MELEE = ConvertGameType(1)
    constant gametype GAME_TYPE_FFA = ConvertGameType(2)
    constant mapflag MAP_FOG_HIDE_TERRAIN = ConvertMapFlag(1)
    constant mapflag MAP_FOG_MAP_EXPLORED = ConvertMapFlag(2)
    constant placement MAP_PLACEMENT_FIXED = ConvertPlacement(1)
    constant startlocprio MAP_LOC_PRIO_HIGH = ConvertStartLocPrio(1)
    constant startlocprio MAP_LOC_PRIO_NOT = ConvertStartLocPrio(2)
    constant mapdensity MAP_DENSITY_LIGHT = ConvertMapDensity(1)
    constant mapdensity MAP_DENSITY_HEAVY = ConvertMapDensity(3)
    constant gamedifficulty MAP_DIFFICULTY_HARD = ConvertGameDifficulty(2)
    constant gamespeed MAP_SPEED_FAST = ConvertGameSpeed(3)
    constant playerstate PLAYER_STATE_RESOURCE_GOLD = ConvertPlayerState(1)
endglobals
