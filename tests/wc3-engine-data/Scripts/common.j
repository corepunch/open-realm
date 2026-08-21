// Minimal common.j for WC3 in-engine test fixtures.
// Declares only the types and natives exercised by the test suite.
// Not a substitute for the real common.j — do not add production map content here.

// Handle subtypes.
type unit             extends handle
type player           extends handle
type quest            extends handle
type questitem        extends handle
type playergameresult extends handle

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
    constant playergameresult PLAYER_GAME_RESULT_VICTORY = ConvertPlayerGameResult(0)
    constant playergameresult PLAYER_GAME_RESULT_DEFEAT  = ConvertPlayerGameResult(1)
    constant playergameresult PLAYER_GAME_RESULT_TIE     = ConvertPlayerGameResult(2)
    constant playergameresult PLAYER_GAME_RESULT_NEUTRAL = ConvertPlayerGameResult(3)
endglobals
