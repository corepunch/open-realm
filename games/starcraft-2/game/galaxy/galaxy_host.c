/* SC2 Galaxy host. Missing bindings raise protected script errors; see galaxy-native-coverage.json for legacy stubs. */

#include "galaxy_host.h"
#include "games/warcraft-3/jass/jass_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* -------------------------------------------------------------------------
 * ReadFile fallback for Galaxy script includes.
 * Tries VFS first, then <script_dir>/<filename>, then <filename> directly.
 * ------------------------------------------------------------------------- */
static char sc2_gdir[512] = "data/TRaynor01-galaxy";
static handle_t (*sc2_orig_readfile)(cstring_t, uint32_t *);
static handle_t (*sc2_memalloc)(long);
static void   (*sc2_memfree)(handle_t);

void galaxy_set_script_dir(cstring_t dir) {
    if (dir) snprintf(sc2_gdir, sizeof(sc2_gdir), "%s", dir);
}

static handle_t sc2_galaxy_readfile(cstring_t filename, uint32_t *size) {
    char path[1024];
    FILE *f;
    long sz;
    string_t buf;

    if (sc2_orig_readfile) {
        handle_t h = sc2_orig_readfile(filename, size);
        if (h) return h;
    }
    snprintf(path, sizeof(path), "%s/%s", sc2_gdir, filename);
    f = fopen(path, "rb");
    if (!f) f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    buf = sc2_memalloc ? (string_t)sc2_memalloc((long)sz + 1) : (string_t)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        if (sc2_memfree) sc2_memfree(buf); else free(buf);
        return NULL;
    }
    buf[sz] = '\0';
    *size = (uint32_t)sz;
    return buf;
}

/* -------------------------------------------------------------------------
 * Callbacks into g_sc2.c — set during SC2_InitGalaxyHost()
 * ------------------------------------------------------------------------- */
void (*sc2_galaxy_on_camera)(float target_x, float target_y,
                             float yaw, float pitch,
                             float dist, float fov, float height_offset, float duration);
void (*sc2_galaxy_on_cinematic)(bool enable, float duration);
void (*sc2_galaxy_on_fade)(float alpha, float duration);
cstring_t (*sc2_galaxy_conversation_field)(cstring_t key, cstring_t field);
float (*sc2_galaxy_sound_length)(cstring_t sound_id, int asset);
void (*sc2_galaxy_on_sound)(cstring_t sound_id, int asset);
void *(*sc2_galaxy_on_unit_create)(cstring_t model, int player,
                                   float x, float y, float angle);

bool (*sc2_galaxy_get_camera_by_id)(uint32_t map_id,
    float *tx, float *ty, float *tz,
    float *pitch, float *yaw, float *dist, float *fov, float *height_offset);
bool (*sc2_galaxy_get_point_by_id)(uint32_t map_id, float *x, float *y);
char const *(*sc2_galaxy_get_unit_model)(cstring_t unit_type);
void (*sc2_galaxy_unit_set_position)(void *ent, float x, float y, float facing);
void (*sc2_galaxy_unit_move)(void *ent, float x, float y);
bool (*sc2_galaxy_unit_is_moving)(void *ent);
bool (*sc2_galaxy_unit_is_alive)(void *ent);
int (*sc2_galaxy_unit_owner)(void *ent);

sc2UnitState_t *(*sc2_galaxy_unit_state)(void *ent);
bool (*sc2_galaxy_unit_location)(void *ent, float *x, float *y, float *z, float *facing);
void *(*sc2_galaxy_unit_from_id)(uint32_t map_id);
void (*sc2_galaxy_unit_changed)(void *ent);
void (*sc2_galaxy_unit_remove)(void *ent);
void (*sc2_galaxy_unit_set_owner)(void *ent, int player, bool change_color);

void (*sc2_galaxy_on_actor_create)(unsigned actor_id, char const *model,
                                   unsigned unit_id, float x, float y);
void (*sc2_galaxy_on_actor_send)(unsigned actor_id, char const *msg);
void (*sc2_galaxy_on_actor_destroy)(unsigned actor_id);

/* -------------------------------------------------------------------------
 * Domain modules — each brings its own state, helpers, and native functions.
 * ------------------------------------------------------------------------- */
#include "galaxy_collection.h"
#include "galaxy_player.h"
#include "galaxy_trigger.h"
#include "galaxy_point.h"
#include "galaxy_catalog.h"
#include "galaxy_unit.h"
#include "galaxy_camera.h"
#include "galaxy_cinematic.h"
#include "galaxy_sound.h"
#include "galaxy_transmission.h"
#include "galaxy_game.h"
#include "galaxy_ui.h"
#include "galaxy_techtree.h"
#include "galaxy_actor.h"
#include "galaxy_math.h"

/* -------------------------------------------------------------------------
 * VM lifecycle — resets all domain state and drives the JASS VM.
 * ------------------------------------------------------------------------- */
/* Defined in jdo.c — not part of the public JASS API; owned by this subsystem. */
void galaxy_loaded_reset(void);

void galaxy_reset(void) {
    sc2_objectives_reset();
    sc2_collections_reset();
    memset(sc2_players,0,sizeof(sc2_players));
    memset(sc2_gunits,0,sizeof(sc2_gunits));
    memset(sc2_regions,0,sizeof(sc2_regions));
    memset(sc2_region_nodes,0,sizeof(sc2_region_nodes));
    sc2_region_n = sc2_region_node_n = 1;
    sc2_last_unit_group = 0;

    for (uint32_t i = 0; i < sc2_trig_n; i++) {
        free((void *)sc2_trigs[i].func);  /* free strdup'd names */
        sc2_trigs[i].func = NULL;
    }
    memset(sc2_trigs, 0, sizeof(sc2_trigs));
    sc2_trig_n = 0;
    sc2_trig_next_id = 1;
    sc2_gunit_n = 0;
    sc2_last_unit_handle = 0;
    sc2_gpoint_n = 1;
    sc2_gcam_n = 1;
    memset(sc2_gcargo,   0, sizeof(sc2_gcargo));
    memset(sc2_gcargo_n, 0, sizeof(sc2_gcargo_n));
    sc2_last_cargo_handle = 0;
    memset(sc2_gabilcmds, 0, sizeof(sc2_gabilcmds));
    sc2_gabilcmd_n = 1;
    memset(sc2_gorders, 0, sizeof(sc2_gorders));
    sc2_gorder_n = 1;
    memset(sc2_uorders, 0, sizeof(sc2_uorders));
    memset(sc2_uorder_n, 0, sizeof(sc2_uorder_n));
    memset(sc2_gsounds, 0, sizeof(sc2_gsounds));
    sc2_gsound_n = 1;
    memset(sc2_gactors, 0, sizeof(sc2_gactors));
    sc2_gactor_n = 0;
    sc2_last_actor_handle = 0;
    sc2_scope_n = sc2_scope_last = 0;
    memset(sc2_scopes, 0, sizeof(sc2_scopes));
    memset(sc2_unit_scope, 0, sizeof(sc2_unit_scope));
    memset(sc2_int_loops, 0, sizeof(sc2_int_loops));

    galaxy_loaded_reset();
}

void galaxy_fire_mapinit(jass_t *j) {
    fprintf(stderr, "galaxy_fire_mapinit: %u triggers registered, firing MapInit\n", sc2_trig_n);
    for (uint32_t i = 0; i < sc2_trig_n; i++) {
        if (sc2_trigs[i].mapinit && sc2_trigs[i].func) {
            fprintf(stderr, "  firing MapInit trigger: %s\n", sc2_trigs[i].func);
            sc2_fire_trigger_func(j, sc2_trigs[i].func, false, true);
        }
    }
    jass_runevents(j);
}

jass_t *galaxy_open(handle_t (*readfile)(cstring_t, uint32_t *),
                   uint32_t  (*gettime)(void),
                   handle_t (*memalloc)(long),
                   void   (*memfree)(handle_t)) {
    char path[512];
    sc2_orig_readfile = readfile;
    sc2_memalloc      = memalloc;
    sc2_memfree       = memfree;
    jass_sethost(&(jassHost_t){
        .MemAlloc       = memalloc,
        .MemFree        = memfree,
        .GetTime        = gettime,
        .ReadFile       = sc2_galaxy_readfile,
        .galaxy_natives = galaxy_get_natives(),
    });
    jass_t *vm = jass_newstate();
    /* Load MapScript.galaxy only — it includes NativeLib/LibertyLib/CampaignLib
     * via its own `include` directives, each parsed exactly once.  Pre-loading
     * them separately causes each to be re-parsed 4–7 times via nested includes,
     * making initialization O(n^2) in the JASS VM's linked-list variable lookup. */
    strlcpy(path, sc2_gdir, sizeof(path));
    strlcat(path, "/MapScript.galaxy", sizeof(path));
    if (!jass_dofile(vm, path))
        fprintf(stderr, "galaxy_open: failed to load MapScript.galaxy from %s\n", sc2_gdir);
    if (jass_rterror_pending(vm)) {
        fprintf(stderr, "galaxy_open: error: %s\n", jass_rterror_message(vm));
        jass_rterror_clear(vm);
    }
    return vm;
}

void galaxy_close(jass_t *vm) {
    if (vm) {
        for (uint32_t i = 0; i < jass_missingcount(vm); i++)
            fprintf(stderr, "galaxy: missing function: %s\n", jass_missingname(vm, i));
        jass_close(vm);
    }
    galaxy_reset();
}

void galaxy_start(jass_t *vm) {
    /* TODO: InitLibs reaches unimplemented dialog/purchase event producers. Keep this gap explicit until wired. */
    fprintf(stderr, "galaxy_start: warning: InitLibs is not yet supported (library event bindings incomplete)\n");
    static cstring_t const entry[] = { "InitGlobals", "InitTriggers" };
    for (uint32_t i = 0; i < sizeof(entry) / sizeof(*entry); i++) {
        fprintf(stderr, "galaxy_start: calling %s\n", entry[i]);
        jass_callbyname(vm, entry[i], false);
        if (jass_rterror_pending(vm)) {
            fprintf(stderr, "galaxy_start: %s error: %s\n", entry[i], jass_rterror_message(vm));
            jass_rterror_clear(vm);
            return;
        }
    }
    fprintf(stderr, "galaxy_start: done, %u triggers registered\n", sc2_trig_n);
}

void galaxy_tick(jass_t *vm) {
    sc2_run_unit_orders();
    jass_runevents(vm);
    if (jass_rterror_pending(vm)) {
        fprintf(stderr, "galaxy: runtime error: %s\n", jass_rterror_message(vm));
        jass_rterror_clear(vm);
    }
}

/* -------------------------------------------------------------------------
 * Native table
 * ------------------------------------------------------------------------- */
static jassModule_t sc2_galaxy_natives[] = {
    { "UnitSetPropertyInt", sc2_UnitSetPropertyInt },
    { "UnitGetPropertyInt", sc2_UnitGetPropertyInt },
    { "UnitSetCustomValue", sc2_UnitSetCustomValue },
    { "UnitGetCustomValue", sc2_UnitGetCustomValue },
    { "PlayerGroupLoopCurrent", sc2_PlayerGroupLoopCurrent },
    { "PlayerModifyPropertyFixed", sc2_PlayerModifyPropertyFixed },
    { "PlayerGetPropertyInt", sc2_PlayerGetPropertyInt },
    { "PlayerGetPropertyFixed", sc2_PlayerGetPropertyFixed },
    { "PlayerSetDifficulty", sc2_PlayerSetDifficulty },
    { "PointSetHeight", sc2_PointSetHeight },
    { "PointSet", sc2_PointSet },
    { "PointsInRange", sc2_PointsInRange },
    { "RegionSetOffset", sc2_RegionSetOffset },
    { "OrderGetPlayer", sc2_OrderGetPlayer },
    { "OrderSetAbilityCommand", sc2_OrderSetAbilityCommand },
    { "OrderGetAbilityCommand", sc2_OrderGetAbilityCommand },
    { "OrderGetTargetType", sc2_OrderGetTargetType },
    { "OrderSetTargetPoint", sc2_OrderSetTargetPoint },
    { "OrderGetTargetPoint", sc2_OrderGetTargetPoint },
    { "OrderSetTargetUnit", sc2_OrderSetTargetUnit },
    { "OrderGetTargetUnit", sc2_OrderGetTargetUnit },
    { "OrderSetFlag", sc2_OrderSetFlag },
    { "OrderGetFlag", sc2_OrderGetFlag },
    { "OrderGetTargetPosition", sc2_OrderGetTargetPosition },

    { "ACos",                                sc2_ACos },
    { "ASin",                                sc2_ASin },
    { "ATan",                                sc2_ATan },
    { "ATan2",                               sc2_ATan2 },
    { "AbilityClass",                        sc2_AbilityClass },
    { "AbilityCommand",                      sc2_AbilityCommand },
    { "AbilityCommandGetAbility",            sc2_AbilityCommandGetAbility },
    { "AbilityCommandGetCommand",            sc2_AbilityCommandGetCommand },
    { "AbilityCommandGetAction",             sc2_AbilityCommandGetAction },
    { "AbsF",                                sc2_AbsF },
    { "AchievementAward",                    sc2_AchievementAward },
    { "AchievementErase",                    sc2_AchievementErase },
    { "AchievementPanelSetCategory",         sc2_AchievementPanelSetCategory },
    { "AchievementPanelSetVisible",          sc2_AchievementPanelSetVisible },
    { "AchievementPercentText",              sc2_AchievementPercentText },
    { "AchievementTermQuantitySet",          sc2_AchievementTermQuantitySet },
    { "AchievementsDisable",                 sc2_AchievementsDisable },
    { "ActorCreate",                         sc2_ActorCreate },
    { "ActorFrom",                           sc2_ActorFrom },
    { "ActorFromScope",                      sc2_ActorFromScope },
    { "ActorRegionCreate",                   sc2_ActorRegionCreate },
    { "ActorRegionSend",                     sc2_ActorRegionSend },
    { "ActorScopeFrom",                      sc2_ActorScopeFrom },
    { "ActorScopeFromActor",                 sc2_ActorScopeFromActor },
    { "ActorScopeKill",                      sc2_ActorScopeKill },
    { "ActorScopeFromUnit",                  sc2_ActorScopeFromUnit },
    { "ActorSend",                           sc2_ActorSend },
    { "AIDisableAllScouting",                sc2_AIDisableAllScouting },
    { "AITimePause",                         sc2_AITimePause },
    { "AngleBetweenPoints",                  sc2_AngleBetweenPoints },
    { "BankExists",                          sc2_BankExists },
    { "BankKeyRemove",                       sc2_BankKeyRemove },
    { "BankLastCreated",                     sc2_BankLastCreated },
    { "BankLoad",                            sc2_BankLoad },
    { "BankSave",                            sc2_BankSave },
    { "BankValueSetFromFlag",                sc2_BankValueSetFromFlag },
    { "BankValueSetFromInt",                 sc2_BankValueSetFromInt },
    { "BankValueSetFromString",              sc2_BankValueSetFromString },
    { "BankValueSetFromText",                sc2_BankValueSetFromText },
    { "CameraApplyInfo",                     sc2_CameraApplyInfo },
    { "CameraGetTarget",                     sc2_CameraGetTarget },
    { "CameraInfoDefault",                   sc2_CameraInfoDefault },
    { "CameraInfoFromId",                    sc2_CameraInfoFromId },
    { "CameraLockInput",                     sc2_CameraLockInput },
    { "CameraPan",                           sc2_CameraPan },
    { "CameraRestore",                       sc2_CameraRestore },
    { "CameraSave",                          sc2_CameraSave },
    { "CameraShakeStart",                    sc2_CameraShakeStart },
    { "CampaignMode",                        sc2_CampaignMode },
    { "CatalogEntryClass",                   sc2_CatalogEntryClass },
    { "CatalogEntryCount",                   sc2_CatalogEntryCount },
    { "CatalogEntryGet",                     sc2_CatalogEntryGet },
    { "CatalogEntryIsValid",                 sc2_CatalogEntryIsValid },
    { "CatalogEntryParent",                  sc2_CatalogEntryParent },
    { "CatalogEntryScope",                   sc2_CatalogEntryScope },
    { "CatalogFieldCount",                   sc2_CatalogFieldCount },
    { "CatalogFieldGet",                     sc2_CatalogFieldGet },
    { "CatalogFieldIsArray",                 sc2_CatalogFieldIsArray },
    { "CatalogFieldIsScope",                 sc2_CatalogFieldIsScope },
    { "CatalogFieldType",                    sc2_CatalogFieldType },
    { "CatalogFieldValueCount",              sc2_CatalogFieldValueCount },
    { "CatalogFieldValueGet",                sc2_CatalogFieldValueGet },
    { "CatalogFieldValueSet",                sc2_CatalogFieldValueSet },
    { "CinematicDataRun",                    sc2_CinematicDataRun },
    { "CinematicDataStop",                   sc2_CinematicDataStop },
    { "CinematicFade",                       sc2_CinematicFade },
    { "CinematicMode",                       sc2_CinematicMode },
    { "CinematicOverlay",                    sc2_CinematicOverlay },
    { "Color",                               sc2_Color },
    { "ColorWithAlpha",                      sc2_ColorWithAlpha },
    { "ConversationDataResetNodeState",       sc2_ConversationDataResetNodeState },
    { "ConversationDataResetStateValues",    sc2_ConversationDataResetStateValues },
    { "ConversationDataSaveNodeState",       sc2_ConversationDataSaveNodeState },
    { "ConversationDataSaveStateValues",     sc2_ConversationDataSaveStateValues },
    { "ConversationDataStateFixedValue",     sc2_ConversationDataStateFixedValue },
    { "ConversationDataStateGetValue",       sc2_ConversationDataStateGetValue },
    { "ConversationDataStateImagePath",     sc2_ConversationDataStateImagePath },
    { "ConversationDataStateIndex",          sc2_ConversationDataStateIndex },
    { "ConversationDataStateIndexCount",     sc2_ConversationDataStateIndexCount },
    { "ConversationDataStateName",           sc2_ConversationDataStateName },
    { "ConversationDataStateSetValue",       sc2_ConversationDataStateSetValue },
    { "ConversationDataStateText",           sc2_ConversationDataStateText },
    { "Cos",                                 sc2_Cos },
    { "DataTableSetString",                  sc2_DataTableSetString },
    { "DataTableValueExists",                sc2_DataTableValueExists },
    { "DialogControlSetPropertyAsText",      sc2_DialogControlSetPropertyAsText },
    { "DialogControlSetVisible",             sc2_DialogControlSetVisible },
    { "DifficultyEnabled",                   sc2_DifficultyEnabled },
    { "DifficultyName",                      sc2_DifficultyName },
    { "DifficultyNameCampaign",              sc2_DifficultyNameCampaign },
    { "DistanceBetweenPoints",               sc2_DistanceBetweenPoints },
    { "EventUnit",                           sc2_EventUnit },
    { "EventUnitCargo",                      sc2_EventUnitCargo },
    { "EventUnitTarget",                     sc2_EventUnitTarget },
    { "FixedToInt",                          sc2_FixedToInt },
    { "FixedToString",                       sc2_FixedToString },
    { "FormatNumber",                        sc2_FormatNumber },
    { "GameCheatAllow",                      sc2_GameCheatAllow },
    { "GameGetSpeedValue",                   sc2_GameGetSpeedValue },
    { "GameIsDebugOptionSet",                sc2_GameIsDebugOptionSet },
    { "GameIsTestMap",                       sc2_GameIsTestMap },
    { "GameIsTransitionMap",                 sc2_GameIsTransitionMap },
    { "GameMapIsBlizzard",                   sc2_GameMapIsBlizzard },
    { "GamePauseAllCharges",                 sc2_GamePauseAllCharges },
    { "GameSetBackground",                   sc2_GameSetBackground },
    { "GameSetLighting",                     sc2_GameSetLighting },
    { "GameSetSeedLocked",                   sc2_GameSetSeedLocked },
    { "GameSetSpeedLocked",                  sc2_GameSetSpeedLocked },
    { "GameSetSpeedValue",                   sc2_GameSetSpeedValue },
    { "GameTimeOfDayPause",                  sc2_GameTimeOfDayPause },
    { "GameTimeOfDaySet",                    sc2_GameTimeOfDaySet },
    { "HelpPanelAddTip",                     sc2_HelpPanelAddTip },
    { "HelpPanelDisplayPage",                sc2_HelpPanelDisplayPage },
    { "HelpPanelAddTutorial",                sc2_HelpPanelAddTutorial },
    { "HelpPanelEnableTechTreeButton",       sc2_HelpPanelEnableTechTreeButton },
    { "HelpPanelShowTechTreeRace",           sc2_HelpPanelShowTechTreeRace },
    { "IntLoopBegin",                        sc2_IntLoopBegin },
    { "IntLoopDone",                         sc2_IntLoopDone },
    { "IntLoopEnd",                          sc2_IntLoopEnd },
    { "IntLoopStep",                         sc2_IntLoopStep },
    { "IntToFixed",                          sc2_IntToFixed },
    { "IntToString",                         sc2_IntToString },
    { "IntToText",                           sc2_IntToText },
    { "MaxF",                                sc2_MaxF },
    { "MinF",                                sc2_MinF },
    { "MinimapPing",                         sc2_MinimapPing },
    { "ModF",                                sc2_ModF },
    { "ObjectiveCreate",                     sc2_ObjectiveCreate },
    { "ObjectiveDestroy",                    sc2_ObjectiveDestroy },
    { "ObjectiveGetName",                    sc2_ObjectiveGetName },
    { "ObjectiveGetDescription",             sc2_ObjectiveGetDescription },
    { "ObjectiveCreate3",                    sc2_ObjectiveCreate3 },
    { "ObjectiveGetPrimary",                 sc2_ObjectiveGetPrimary },
    { "ObjectiveGetState",                   sc2_ObjectiveGetState },
    { "ObjectiveLastCreated",                sc2_ObjectiveLastCreated },
    { "ObjectiveSetName",                    sc2_ObjectiveSetName },
    { "ObjectiveSetState",                   sc2_ObjectiveSetState },
    { "Order",                               sc2_Order },
    { "OrderSetPlayer",                      sc2_OrderSetPlayer },
    { "OrderTargetingPoint",                 sc2_OrderTargetingPoint },
    { "OrderTargetingUnit",                  sc2_OrderTargetingUnit },
    { "UnitOrderIsValid",                    sc2_UnitOrderIsValid },
    { "PingCreate",                          sc2_PingCreate },
    { "PingDestroy",                         sc2_PingDestroy },
    { "PingLastCreated",                     sc2_PingLastCreated },
    { "PingSetScale",                        sc2_PingSetScale },
    { "PingSetTooltip",                      sc2_PingSetTooltip },
    { "PlayerAddChargeRegen",                sc2_PlayerAddChargeRegen },
    { "PlayerAddChargeUsed",                 sc2_PlayerAddChargeUsed },
    { "PlayerAddCooldown",                   sc2_PlayerAddCooldown },
    { "PlayerBeaconAlert",                   sc2_PlayerBeaconAlert },
    { "PlayerBeaconClearTarget",             sc2_PlayerBeaconClearTarget },
    { "PlayerBeaconGetTargetPoint",          sc2_PlayerBeaconGetTargetPoint },
    { "PlayerBeaconGetTargetUnit",           sc2_PlayerBeaconGetTargetUnit },
    { "PlayerBeaconIsAutoCast",              sc2_PlayerBeaconIsAutoCast },
    { "PlayerBeaconIsFromUser",              sc2_PlayerBeaconIsFromUser },
    { "PlayerBeaconIsSet",                   sc2_PlayerBeaconIsSet },
    { "PlayerBeaconSetAutoCast",             sc2_PlayerBeaconSetAutoCast },
    { "PlayerBeaconSetTargetPoint",          sc2_PlayerBeaconSetTargetPoint },
    { "PlayerBeaconSetTargetUnit",           sc2_PlayerBeaconSetTargetUnit },
    { "PlayerCreateEffectPoint",             sc2_PlayerCreateEffectPoint },
    { "PlayerCreateEffectUnit",              sc2_PlayerCreateEffectUnit },
    { "PlayerDifficulty",                    sc2_PlayerDifficulty },
    { "PlayerGetAlliance",                   sc2_PlayerGetAlliance },
    { "PlayerGetChargeRegen",                sc2_PlayerGetChargeRegen },
    { "PlayerGetChargeUsed",                 sc2_PlayerGetChargeUsed },
    { "PlayerGetCooldown",                   sc2_PlayerGetCooldown },
    { "PlayerGetState",                      sc2_PlayerGetState },
    { "PlayerGroupActive",                   sc2_PlayerGroupActive },
    { "PlayerGroupAdd",                      sc2_PlayerGroupAdd },
    { "PlayerGroupAll",                      sc2_PlayerGroupAll },
    { "PlayerGroupAlliance",                 sc2_PlayerGroupAlliance },
    { "PlayerGroupClear",                    sc2_PlayerGroupClear },
    { "PlayerGroupCopy",                     sc2_PlayerGroupCopy },
    { "PlayerGroupCount",                    sc2_PlayerGroupCount },
    { "PlayerGroupEmpty",                    sc2_PlayerGroupEmpty },
    { "PlayerGroupHasPlayer",                sc2_PlayerGroupHasPlayer },
    { "PlayerGroupLoopBegin",                sc2_PlayerGroupLoopBegin },
    { "PlayerGroupLoopDone",                 sc2_PlayerGroupLoopDone },
    { "PlayerGroupLoopEnd",                  sc2_PlayerGroupLoopEnd },
    { "PlayerGroupLoopStep",                 sc2_PlayerGroupLoopStep },
    { "PlayerGroupPlayer",                   sc2_PlayerGroupPlayer },
    { "PlayerGroupRemove",                   sc2_PlayerGroupRemove },
    { "PlayerGroupSingle",                   sc2_PlayerGroupSingle },
    { "PlayerModifyPropertyInt",             sc2_PlayerModifyPropertyInt },
    { "PlayerPauseAllCharges",               sc2_PlayerPauseAllCharges },
    { "PlayerPauseAllCooldowns",             sc2_PlayerPauseAllCooldowns },
    { "PlayerScoreValueEnable",              sc2_PlayerScoreValueEnable },
    { "PlayerScoreValueEnableAll",           sc2_PlayerScoreValueEnableAll },
    { "PlayerScoreValueGetAsFixed",          sc2_PlayerScoreValueGetAsFixed },
    { "PlayerScoreValueGetAsInt",            sc2_PlayerScoreValueGetAsInt },
    { "PlayerScoreValueSetFromFixed",        sc2_PlayerScoreValueSetFromFixed },
    { "PlayerScoreValueSetFromInt",          sc2_PlayerScoreValueSetFromInt },
    { "PlayerSetAlliance",                   sc2_PlayerSetAlliance },
    { "PlayerSetState",                      sc2_PlayerSetState },
    { "PlayerType",                          sc2_PlayerType },
    { "PlayerValidateEffectPoint",           sc2_PlayerValidateEffectPoint },
    { "PlayerValidateEffectUnit",            sc2_PlayerValidateEffectUnit },
    { "Point",                               sc2_Point },
    { "PointFromId",                         sc2_PointFromId },
    { "PointGetFacing",                      sc2_PointGetFacing },
    { "PointGetHeight",                      sc2_PointGetHeight },
    { "PointGetX",                           sc2_PointGetX },
    { "PointGetY",                           sc2_PointGetY },
    { "PointPathingCliffLevel",              sc2_PointPathingCliffLevel },
    { "PointReflect",                        sc2_PointReflect },
    { "PointSetFacing",                      sc2_PointSetFacing },
    { "PointWithOffset",                     sc2_PointWithOffset },
    { "PointWithOffsetPolar",                sc2_PointWithOffsetPolar },
    { "Pow",                                 sc2_Pow },
    { "PreloadAsset",                        sc2_PreloadAsset },
    { "PreloadImage",                        sc2_PreloadImage },
    { "PreloadModel",                        sc2_PreloadModel },
    { "PreloadMovie",                        sc2_PreloadMovie },
    { "PreloadObject",                       sc2_PreloadObject },
    { "PreloadScene",                        sc2_PreloadScene },
    { "PreloadScript",                       sc2_PreloadScript },
    { "PreloadSound",                        sc2_PreloadSound },
    { "RandomFixed",                         sc2_RandomFixed },
    { "RandomInt",                           sc2_RandomInt },
    { "RegionAddCircle",                     sc2_RegionAddCircle },
    { "RegionAddRect",                       sc2_RegionAddRect },
    { "RegionAddRegion",                     sc2_RegionAddRegion },
    { "RegionAttachToUnit",                  sc2_RegionAttachToUnit },
    { "RegionCircle",                        sc2_RegionCircle },
    { "RegionContainsPoint",                 sc2_RegionContainsPoint },
    { "RegionEmpty",                         sc2_RegionEmpty },
    { "RegionEntireMap",                     sc2_RegionEntireMap },
    { "RegionFromId",                        sc2_RegionFromId },
    { "RegionGetAttachUnit",                 sc2_RegionGetAttachUnit },
    { "RegionGetBoundsMax",                  sc2_RegionGetBoundsMax },
    { "RegionGetBoundsMin",                  sc2_RegionGetBoundsMin },
    { "RegionGetCenter",                     sc2_RegionGetCenter },
    { "RegionGetOffset",                     sc2_RegionGetOffset },
    { "RegionPlayableMap",                   sc2_RegionPlayableMap },
    { "RegionPlayableMapSet",                sc2_RegionPlayableMapSet },
    { "RegionRandomPoint",                   sc2_RegionRandomPoint },
    { "RegionRect",                          sc2_RegionRect },
    { "RegionSetCenter",                     sc2_RegionSetCenter },
    { "Sin",                                 sc2_Sin },
    { "SoundLink",                           sc2_SoundLink },
    { "SoundLinkAsset",                      sc2_SoundLinkAsset },
    { "SoundLinkId",                         sc2_SoundLinkId },
    { "SoundLengthSync",                     sc2_SoundLengthSync },
    { "SoundPlay",                           sc2_SoundPlay },
    { "SoundPlayAtPoint",                    sc2_SoundPlayAtPoint },
    { "SoundPlayOnUnit",                     sc2_SoundPlayOnUnit },
    { "SoundPlayScene",                      sc2_SoundPlayScene },
    { "SoundPlaySceneFile",                  sc2_SoundPlaySceneFile },
    { "SoundChannelSetVolume",               sc2_SoundChannelSetVolume },
    { "SoundStop",                           sc2_SoundStop },
    { "SoundWait",                           sc2_SoundWait },
    { "SoundtrackDefault",                   sc2_SoundtrackDefault },
    { "SoundtrackPause",                     sc2_SoundtrackPause },
    { "SoundtrackPlay",                      sc2_SoundtrackPlay },
    { "SquareRoot",                          sc2_SquareRoot },
    { "StringExternal",                      sc2_StringExternal },
    { "StringReplaceWord",                   sc2_StringReplaceWord },
    { "StringSub",                           sc2_StringSub },
    { "StringToText",                        sc2_StringToText },
    { "StringWord",                          sc2_StringWord },
    { "Tan",                                 sc2_Tan },
    { "TechTreeAbilityAllow",                sc2_TechTreeAbilityAllow },
    { "TechTreeAbilityIsAllowed",            sc2_TechTreeAbilityIsAllowed },
    { "TechTreeRestrictionsEnable",          sc2_TechTreeRestrictionsEnable },
    { "TechTreeUnitHelp",                    sc2_TechTreeUnitHelp },
    { "TechTreeUnitHelpDefault",             sc2_TechTreeUnitHelpDefault },
    { "TechTreeUpgradeAddLevel",             sc2_TechTreeUpgradeAddLevel },
    { "TechTreeUpgradeCount",                sc2_TechTreeUpgradeCount },
    { "TextCase",                            sc2_TextCase },
    { "TimerPause",                          sc2_TimerPause },
    { "TransmissionClear",                   sc2_TransmissionClear },
    { "TransmissionClearAll",                sc2_TransmissionClearAll },
    { "TransmissionLastSent",                sc2_TransmissionLastSent },
    { "TransmissionSend",                    sc2_TransmissionSend },
    { "TransmissionSource",                  sc2_TransmissionSource },
    { "TransmissionSourceFromModel",         sc2_TransmissionSourceFromModel },
    { "TransmissionSetOption",               sc2_TransmissionSetOption },
    { "TransmissionSourceFromUnit",          sc2_TransmissionSourceFromUnit },
    { "TransmissionWait",                    sc2_TransmissionWait },
    { "TriggerAddEventMapInit",              sc2_TriggerAddEventMapInit },
    { "TriggerAddEventPlayerAIWave",         sc2_TriggerAddEventPlayerAIWave },
    { "TriggerAddEventPlayerAllianceChange", sc2_TriggerAddEventPlayerAllianceChange },
    { "TriggerAddEventPlayerLeft",           sc2_TriggerAddEventPlayerLeft },
    { "TriggerAddEventPlayerPropChange",     sc2_TriggerAddEventPlayerPropChange },
    { "TriggerAddEventTimeElapsed",          sc2_TriggerAddEventTimeElapsed },
    { "TriggerAddEventTimePeriodic",         sc2_TriggerAddEventTimePeriodic },
    { "TriggerAddEventTimer",                sc2_TriggerAddEventTimer },
    { "TriggerAddEventUnitAttacked",         sc2_TriggerAddEventUnitAttacked },
    { "TriggerAddEventUnitCargo",            sc2_TriggerAddEventUnitCargo },
    { "TriggerAddEventUnitDamaged",          sc2_TriggerAddEventUnitDamaged },
    { "TriggerAddEventUnitDied",             sc2_TriggerAddEventUnitDied },
    { "TriggerAddEventUnitOrder",            sc2_TriggerAddEventUnitOrder },
    { "TriggerAddEventUnitRange",            sc2_TriggerAddEventUnitRange },
    { "TriggerAddEventUnitRangePoint",       sc2_TriggerAddEventUnitRangePoint },
    { "TriggerAddEventUnitRegion",           sc2_TriggerAddEventUnitRegion },
    { "TriggerDebugOutput",                  sc2_TriggerDebugOutput },
    { "TriggerCreate",                       sc2_TriggerCreate },
    { "TriggerEnable",                       sc2_TriggerEnable },
    { "TriggerExecute",                      sc2_TriggerExecute },
    { "TriggerGetCurrent",                   sc2_TriggerGetCurrent },
    { "TriggerGetExecCount",                 sc2_TriggerGetExecCount },
    { "TriggerIsEnabled",                    sc2_TriggerIsEnabled },
    { "TriggerQueueClear",                   sc2_TriggerQueueClear },
    { "TriggerQueueEnter",                   sc2_TriggerQueueEnter },
    { "TriggerQueueExit",                    sc2_TriggerQueueExit },
    { "TriggerQueueIsEmpty",                 sc2_TriggerQueueIsEmpty },
    { "TriggerQueuePause",                   sc2_TriggerQueuePause },
    { "TriggerSkippableBegin",               sc2_TriggerSkippableBegin },
    { "TriggerSkippableEnd",                 sc2_TriggerSkippableEnd },
    { "TriggerStop",                         sc2_TriggerStop },
    { "UIAlertPoint",                        sc2_UIAlertPoint },
    { "UIAlertUnit",                         sc2_UIAlertUnit },
    { "UIClearMessages",                     sc2_UIClearMessages },
    { "UIFlyerHelperClearOverride",          sc2_UIFlyerHelperClearOverride },
    { "UIFlyerHelperOverride",               sc2_UIFlyerHelperOverride },
    { "UIFrameVisible",                      sc2_UIFrameVisible },
    { "UISetCursorVisible",                  sc2_UISetCursorVisible },
    { "UISetFrameVisible",                   sc2_UISetFrameVisible },
    { "UISetGameMenuItemVisible",            sc2_UISetGameMenuItemVisible },
    { "UISetMode",                           sc2_UISetMode },
    { "UISetRestartLoadingScreen",           sc2_UISetRestartLoadingScreen },
    { "UnitBehaviorAdd",                     sc2_UnitBehaviorAdd },
    { "UnitBehaviorRemove",                  sc2_UnitBehaviorRemove },
    { "UnitCargoCreate",                     sc2_UnitCargoCreate },
    { "UnitCargoGroup",                      sc2_UnitCargoGroup },
    { "UnitCargoLastCreated",                sc2_UnitCargoLastCreated },
    { "UnitCargoLastCreatedGroup",           sc2_UnitCargoLastCreatedGroup },
    { "UnitClearInfoText",                   sc2_UnitClearInfoText },
    { "UnitClearSelection",                  sc2_UnitClearSelection },
    { "UnitCreate",                          sc2_UnitCreate },
    { "UnitFilter",                          sc2_UnitFilter },
    { "UnitFilterMatch",                     sc2_UnitFilterMatch },
    { "UnitFilterSetState",                  sc2_UnitFilterSetState },
    { "UnitFilterStr",                       sc2_UnitFilterStr },
    { "UnitForceStatusBar",                  sc2_UnitForceStatusBar },
    { "UnitFromId",                          sc2_UnitFromId },
    { "UnitGetAttachmentPoint",              sc2_UnitGetAttachmentPoint },
    { "UnitGetFacing",                       sc2_UnitGetFacing },
    { "UnitGetHeight",                       sc2_UnitGetHeight },
    { "UnitGetOwner",                        sc2_UnitGetOwner },
    { "UnitGetPosition",                     sc2_UnitGetPosition },
    { "UnitGetPropertyFixed",                sc2_UnitGetPropertyFixed },
    { "UnitGetType",                         sc2_UnitGetType },
    { "UnitGroup",                           sc2_UnitGroup },
    { "UnitGroupAdd",                        sc2_UnitGroupAdd },
    { "UnitGroupAlliance",                   sc2_UnitGroupAlliance },
    { "UnitGroupClear",                      sc2_UnitGroupClear },
    { "UnitGroupCopy",                       sc2_UnitGroupCopy },
    { "UnitGroupCount",                      sc2_UnitGroupCount },
    { "UnitGroupEmpty",                      sc2_UnitGroupEmpty },
    { "UnitGroupFilter",                     sc2_UnitGroupFilter },
    { "UnitGroupFilterAlliance",             sc2_UnitGroupFilterAlliance },
    { "UnitGroupFilterPlane",                sc2_UnitGroupFilterPlane },
    { "UnitGroupFilterPlayer",               sc2_UnitGroupFilterPlayer },
    { "UnitGroupFilterRegion",               sc2_UnitGroupFilterRegion },
    { "UnitGroupFilterThreat",               sc2_UnitGroupFilterThreat },
    { "UnitGroupFromId",                     sc2_UnitGroupFromId },
    { "UnitGroupHasUnit",                    sc2_UnitGroupHasUnit },
    { "UnitGroupIdle",                       sc2_UnitGroupIdle },
    { "UnitGroupIssueOrder",                 sc2_UnitGroupIssueOrder },
    { "UnitGroupLoopBegin",                  sc2_UnitGroupLoopBegin },
    { "UnitGroupLoopCurrent",                sc2_UnitGroupLoopCurrent },
    { "UnitGroupLoopDone",                   sc2_UnitGroupLoopDone },
    { "UnitGroupLoopEnd",                    sc2_UnitGroupLoopEnd },
    { "UnitGroupLoopStep",                   sc2_UnitGroupLoopStep },
    { "UnitGroupNearestUnit",                sc2_UnitGroupNearestUnit },
    { "UnitGroupRandomUnit",                 sc2_UnitGroupRandomUnit },
    { "UnitGroupRemove",                     sc2_UnitGroupRemove },
    { "UnitGroupTestPlane",                  sc2_UnitGroupTestPlane },
    { "UnitGroupUnit",                       sc2_UnitGroupUnit },
    { "UnitGroupWaitUntilIdle",              sc2_UnitGroupWaitUntilIdle },
    { "UnitInventoryGroup",                  sc2_UnitInventoryGroup },
    { "UnitIsAlive",                         sc2_UnitIsAlive },
    { "UnitIsValid",                         sc2_UnitIsValid },
    { "UnitIssueOrder",                      sc2_UnitIssueOrder },
    { "UnitKill",                            sc2_UnitKill },
    { "UnitLastCreated",                     sc2_UnitLastCreated },
    { "UnitLastCreatedGroup",                sc2_UnitLastCreatedGroup },
    { "UnitLoadModel",                       sc2_UnitLoadModel },
    { "UnitPauseAll",                        sc2_UnitPauseAll },
    { "UnitRefFromUnit",                     sc2_UnitRefFromUnit },
    { "UnitRefFromVariable",                 sc2_UnitRefFromVariable },
    { "UnitRefToUnit",                       sc2_UnitRefToUnit },
    { "UnitRemove",                          sc2_UnitRemove },
    { "UnitRevive",                          sc2_UnitRevive },
    { "UnitSetCursor",                       sc2_UnitSetCursor },
    { "UnitSetFacing",                       sc2_UnitSetFacing },
    { "UnitSetHeight",                       sc2_UnitSetHeight },
    { "UnitSetInfoText",                     sc2_UnitSetInfoText },
    { "UnitSetOwner",                        sc2_UnitSetOwner },
    { "UnitSetPosition",                     sc2_UnitSetPosition },
    { "UnitSetPropertyFixed",                sc2_UnitSetPropertyFixed },
    { "UnitSetScale",                        sc2_UnitSetScale },
    { "UnitSetState",                        sc2_UnitSetState },
    { "UnitSetTeamColorIndex",               sc2_UnitSetTeamColorIndex },
    { "UnitTechTreeBehaviorCount",           sc2_UnitTechTreeBehaviorCount },
    { "UnitTechTreeUnitCount",               sc2_UnitTechTreeUnitCount },
    { "UnitTechTreeUpgradeCount",            sc2_UnitTechTreeUpgradeCount },
    { "UnitTestState",                       sc2_UnitTestState },
    { "UnitTypeAnimationLoad",               sc2_UnitTypeAnimationLoad },
    { "UnitTypeAnimationUnload",             sc2_UnitTypeAnimationUnload },
    { "UnitTypeFromString",                  sc2_UnitTypeFromString },
    { "UnitTypeGetCost",                     sc2_UnitTypeGetCost },
    { "UnitTypeGetName",                     sc2_UnitTypeGetName },
    { "UnitTypeGetProperty",                 sc2_UnitTypeGetProperty },
    { "UnitTypeIsAffectedByUpgrade",         sc2_UnitTypeIsAffectedByUpgrade },
    { "UnitTypeTestAttribute",               sc2_UnitTypeTestAttribute },
    { "UnitTypeTestFlag",                    sc2_UnitTypeTestFlag },
    { "UnitUnloadModel",                     sc2_UnitUnloadModel },
    { "UnitWaitUntilIdle",                   sc2_UnitWaitUntilIdle },
    { "VictoryPanelAddAchievement",          sc2_VictoryPanelAddAchievement },
    { "VictoryPanelAddCustomStatisticLine",  sc2_VictoryPanelAddCustomStatisticLine },
    { "VictoryPanelAddTrackedStatistic",     sc2_VictoryPanelAddTrackedStatistic },
    { "VisEnable",                           sc2_VisEnable },
    { "VisExploreArea",                      sc2_VisExploreArea },
    { "VisRevealArea",                       sc2_VisRevealArea },
    { "VisRevealerCreate",                   sc2_VisRevealerCreate },
    { "VisRevealerDestroy",                  sc2_VisRevealerDestroy },
    { "VisRevealerLastCreated",              sc2_VisRevealerLastCreated },
    { "Wait",                                sc2_Wait },
    { NULL, NULL },
};

jassModule_t const *galaxy_get_natives(void) { return sc2_galaxy_natives; }
