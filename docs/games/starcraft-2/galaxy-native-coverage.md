# Galaxy Native Coverage and Implementation Priorities

This is the Markdown companion to [the JSON inventory](galaxy-native-coverage.json), captured from the local client on September 11,
2026 after the TRaynor01 post-intro fixes. It records **818 reachable script functions, 207 missing bindings, and 199 placeholder
candidates**. See [Galaxy scripting](galaxy-scripting.md) for protected errors and runtime evidence, and
[presentation state](galaxy-presentation.md) for the implemented objective/actor/catalog subset.

The September counts predate the event-callback work. Unit, time, timer, player, dialog, chat, purchase, and input `TriggerAddEvent*` / `Event*` natives now keep registrations and a response record. The live contract, including which producers actually fire, is [Event callbacks](galaxy-scripting.md#event-callbacks). Do not treat a "Placeholder candidate" or "Missing" row for those names as the current binding.

## What the Counts Mean

- **Missing**: a call reachable from the audit roots has neither a loaded script-function body nor a name in the SC2 host binding table.
- **Placeholder candidate**: the registered C function matches the tool's simple constant-return/no-op pattern. Read its contract to
  distinguish an incomplete stub from a legitimate constant. Other stub shapes and partial functions are not necessarily detected.
- Audit roots are `InitMap` and every literal `TriggerCreate("callback")` in the supplied sources, including library callbacks.
  Both sides of every conditional are followed; this is not a runtime trace, a map-only call list, or proof that all roots register today.
- The regex-based tool does not preprocess includes, discover arbitrary computed callback names, validate signatures, or prove native
  semantics. Supply all three libraries. It does not count uncalled global initializer expressions as independent roots.
- A successful intro covers only one route through these scripts. A native present in the table can still have the wrong ABI or
  insufficient state behavior; `ActorCreate` demonstrated this directly.

The JSON remains the machine-readable snapshot. Regenerate it with the audit tool and refresh the tables/counts below when changing
coverage; neither representation is a promise of complete campaign conformance.

## Reproduce From Authoritative Archives

From the repository root (local Blizzard assets are required only for this diagnostic, not for unit tests):

```sh
mkdir -p /tmp/sc2-galaxy-audit
build/bin/mpqtool -mpq data/StarCraft2/Campaigns/Liberty.SC2Campaign/Base.SC2Maps cat Maps/Campaign/TRaynor01.SC2Map/MapScript.galaxy > /tmp/sc2-galaxy-audit/MapScript.galaxy
build/bin/mpqtool -mpq data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Data cat TriggerLibs/NativeLib.galaxy > /tmp/sc2-galaxy-audit/NativeLib.galaxy
build/bin/mpqtool -mpq data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Data cat TriggerLibs/natives.galaxy > /tmp/sc2-galaxy-audit/natives.galaxy
build/bin/mpqtool -mpq data/StarCraft2/Mods/Liberty.SC2Mod/Base.SC2Data cat TriggerLibs/LibertyLib.galaxy > /tmp/sc2-galaxy-audit/LibertyLib.galaxy
build/bin/mpqtool -mpq data/StarCraft2/Campaigns/LibertyStory.SC2Campaign/Base.SC2Data cat TriggerLibs/CampaignLib.galaxy > /tmp/sc2-galaxy-audit/CampaignLib.galaxy
python3 tools/galaxy_audit.py /tmp/sc2-galaxy-audit/MapScript.galaxy /tmp/sc2-galaxy-audit/NativeLib.galaxy /tmp/sc2-galaxy-audit/LibertyLib.galaxy /tmp/sc2-galaxy-audit/CampaignLib.galaxy > /tmp/sc2-galaxy-audit/coverage.json
```

Use the extracted `natives.galaxy` to check parameter order and return types. The audit needs script bodies and the local C registry;
it does not need native declarations as an additional input. Preserve archive paths when creating focused test fixtures.

## Proposed Implementation Order

This is an architectural recommendation based on the observed dependencies, not additional completed work:

| Order | Subsystem | Contract/evidence to establish |
|---|---|---|
| 1 | Trigger identity and execution state | Implement current/enabled/stop/count semantics across nested and yielded callbacks; objective-create triggers disable themselves through `TriggerGetCurrent` |
| 2 | Event registration and dispatch | Unit, time, player, and order callbacks retain filters and publish from the server; dialog and purchase filters are stored, and their UI producers are still missing, so `InitLibs` stays off |
| 3 | Map-unit identity and unit groups | Resolve `UnitFromId` from placed-object IDs and live edicts; preserve group membership and iteration for mission conditions |
| 4 | Player ownership and world state | Reconcile script/lobby/client numbering; implement ownership, state queries, and orders against server data rather than constant returns |
| 5 | Combat and mission progress | Generate damage/death/range/region/order events and let authored callbacks update objectives and victory/defeat |
| 6 | Presentation | Bind objective/help/dialog state to authored layouts; resolve actor models/attachments and localization through their native catalogs |
| 7 | Campaign persistence and remaining library UI | Implement bank/conversation/progression contracts and restore full library initialization once dependencies are real |

Use WC3 native wrappers, quest state, trigger execution, and server-authored UI as ownership references. Match the SC2 ABI and source
data rather than importing WC3-specific assumptions. Do not register empty event callbacks solely to get `InitMap` to return.

For each subsystem: inspect the mounted declaration and reachable script uses; capture a bounded failing trace; implement retained
state plus its producer/consumer; add focused fixture regressions; rerun the affected mission route; update this inventory.

## Complete Snapshot by Native Family

Family headings group names for navigation only; they do not introduce new module ownership. Each name appears exactly once.

### AI

| Native | Status |
|---|---|
| `AIDisableAllScouting` | Placeholder candidate |
| `AIGivingUp` | Missing |
| `AIGoodGame` | Missing |
| `AIIsCampaign` | Missing |
| `AITimePause` | Placeholder candidate |

### Achievement

| Native | Status |
|---|---|
| `AchievementAward` | Placeholder candidate |
| `AchievementTermQuantitySet` | Placeholder candidate |

### Achievements

| Native | Status |
|---|---|
| `AchievementsDisable` | Placeholder candidate |

### Actor

| Native | Status |
|---|---|
| `ActorFromScope` | Placeholder candidate |
| `ActorRegionCreate` | Placeholder candidate |
| `ActorRegionSend` | Placeholder candidate |

### Bank

| Native | Status |
|---|---|
| `BankDeleteCampaignBanks` | Missing |
| `BankExists` | Placeholder candidate |
| `BankKeyExists` | Missing |
| `BankKeyRemove` | Placeholder candidate |
| `BankLastCreated` | Placeholder candidate |
| `BankLoad` | Placeholder candidate |
| `BankRemove` | Missing |
| `BankSave` | Placeholder candidate |
| `BankSectionExists` | Missing |
| `BankValueGetAsFlag` | Missing |
| `BankValueGetAsInt` | Missing |
| `BankValueGetAsString` | Missing |
| `BankValueGetAsText` | Missing |
| `BankValueSetFromFlag` | Placeholder candidate |
| `BankValueSetFromInt` | Placeholder candidate |
| `BankValueSetFromString` | Placeholder candidate |
| `BankValueSetFromText` | Placeholder candidate |

### Battle

| Native | Status |
|---|---|
| `BattleReportPanelGetSelectedBattleReport` | Missing |

### Camera

| Native | Status |
|---|---|
| `CameraGetTarget` | Placeholder candidate |
| `CameraInfoDefault` | Placeholder candidate |
| `CameraInfoGetTarget` | Missing |
| `CameraInfoGetValue` | Missing |
| `CameraLockInput` | Placeholder candidate |
| `CameraSetValue` | Missing |
| `CameraShakeStart` | Placeholder candidate |

### Campaign

| Native | Status |
|---|---|
| `CampaignMode` | Placeholder candidate |
| `CampaignProgressSetCampaignFinished` | Missing |
| `CampaignProgressSetImageFilePath` | Missing |
| `CampaignProgressSetText` | Missing |

### Catalog

| Native | Status |
|---|---|
| `CatalogFieldValueGet` | Placeholder candidate |

### Character

| Native | Status |
|---|---|
| `CharacterSheetPanelSetNameText` | Missing |
| `CharacterSheetPanelSetPortraitModelLink` | Missing |

### Color

| Native | Status |
|---|---|
| `Color` | Placeholder candidate |
| `ColorFromIndex` | Missing |
| `ColorWithAlpha` | Placeholder candidate |

### Console

| Native | Status |
|---|---|
| `ConsoleCommand` | Missing |

### Conversation

| Native | Status |
|---|---|
| `ConversationDataLoadNodeState` | Missing |
| `ConversationDataLoadStateValues` | Missing |
| `ConversationDataResetNodeState` | Placeholder candidate |
| `ConversationDataResetStateValues` | Placeholder candidate |
| `ConversationDataSaveNodeState` | Placeholder candidate |
| `ConversationDataSaveStateValue` | Missing |
| `ConversationDataSaveStateValues` | Placeholder candidate |
| `ConversationDataStateAbilCmd` | Missing |
| `ConversationDataStateFixedValue` | Placeholder candidate |
| `ConversationDataStateGetValue` | Placeholder candidate |
| `ConversationDataStateIndex` | Placeholder candidate |
| `ConversationDataStateMoviePath` | Missing |
| `ConversationDataStateSetValue` | Placeholder candidate |
| `ConversationDataStateUpgrade` | Missing |
| `ConversationDataStop` | Missing |

### Data

| Native | Status |
|---|---|
| `DataTableSetString` | Placeholder candidate |
| `DataTableValueExists` | Placeholder candidate |

### Dialog

| Native | Status |
|---|---|
| `DialogControlCreate` | Missing |
| `DialogControlDestroy` | Missing |
| `DialogControlFadeTransparency` | Missing |
| `DialogControlGetHeight` | Missing |
| `DialogControlGetWidth` | Missing |
| `DialogControlLastCreated` | Missing |
| `DialogControlSetEnabled` | Missing |
| `DialogControlSetPosition` | Missing |
| `DialogControlSetPositionRelative` | Missing |
| `DialogControlSetPropertyAsBool` | Missing |
| `DialogControlSetPropertyAsColor` | Missing |
| `DialogControlSetPropertyAsInt` | Missing |
| `DialogControlSetPropertyAsString` | Missing |
| `DialogControlSetPropertyAsText` | Placeholder candidate |
| `DialogControlSetSize` | Missing |
| `DialogControlSetVisible` | Placeholder candidate |
| `DialogCreate` | Missing |
| `DialogDestroy` | Missing |
| `DialogIsVisible` | Missing |
| `DialogLastCreated` | Missing |
| `DialogSetImageVisible` | Missing |
| `DialogSetPosition` | Missing |
| `DialogSetSubtitlePositionOverride` | Missing |
| `DialogSetTransparency` | Missing |
| `DialogSetVisible` | Missing |

### Difficulty

| Native | Status |
|---|---|
| `DifficultyNameCampaign` | Placeholder candidate |

### Event

| Native | Status |
|---|---|
| `EventBattleReportPanelDifficultySelected` | Missing |
| `EventChatMessage` | Missing |
| `EventDialogControl` | Missing |
| `EventPlayer` | Missing |
| `EventPurchaseMade` | Missing |
| `EventUnit` | Placeholder candidate |
| `EventUnitCargo` | Placeholder candidate |
| `EventUnitTarget` | Placeholder candidate |
| `EventVictoryPanelDifficultySelected` | Missing |

### Fixed

| Native | Status |
|---|---|
| `FixedToInt` | Placeholder candidate |
| `FixedToString` | Placeholder candidate |

### Format

| Native | Status |
|---|---|
| `FormatNumber` | Placeholder candidate |

### Game

| Native | Status |
|---|---|
| `GameCheatAllow` | Placeholder candidate |
| `GameCheatsEnabled` | Missing |
| `GameIsDebugOptionSet` | Placeholder candidate |
| `GameIsTestMap` | Placeholder candidate |
| `GameIsTransitionMap` | Placeholder candidate |
| `GameMapIsBlizzard` | Placeholder candidate |
| `GameOver` | Missing |
| `GamePauseAllCharges` | Placeholder candidate |
| `GameSaveCreate` | Missing |
| `GameSetBackground` | Placeholder candidate |
| `GameSetNextMap` | Missing |
| `GameSetSeedLocked` | Placeholder candidate |
| `GameSetSpeedLocked` | Placeholder candidate |
| `GameSetSpeedValue` | Placeholder candidate |
| `GameSetSpeedValueMinimum` | Missing |
| `GameSetTransitionMap` | Missing |
| `GameTimeOfDayPause` | Placeholder candidate |
| `GameTimeOfDaySet` | Placeholder candidate |

### Help

| Native | Status |
|---|---|
| `HelpPanelAddTip` | Placeholder candidate |
| `HelpPanelAddTutorial` | Placeholder candidate |
| `HelpPanelDisplayPage` | Placeholder candidate |
| `HelpPanelEnableTechTreeButton` | Placeholder candidate |
| `HelpPanelShowTechTreeRace` | Placeholder candidate |

### Int

| Native | Status |
|---|---|
| `IntToString` | Placeholder candidate |

### Make

| Native | Status |
|---|---|
| `MakeMsgTextureSelectBySlot` | Missing |

### Max

| Native | Status |
|---|---|
| `MaxI` | Missing |

### Mercenary

| Native | Status |
|---|---|
| `MercenaryCreate` | Missing |
| `MercenaryGetSelected` | Missing |
| `MercenaryLastCreated` | Missing |
| `MercenaryPurchase` | Missing |
| `MercenarySetCost` | Missing |
| `MercenarySetDescriptionText` | Missing |
| `MercenarySetImageFilePath` | Missing |
| `MercenarySetModelLink` | Missing |
| `MercenarySetScenePath` | Missing |
| `MercenarySetSpecialText` | Missing |
| `MercenarySetState` | Missing |
| `MercenarySetTitleText` | Missing |
| `MercenarySetUnitText` | Missing |

### Min

| Native | Status |
|---|---|
| `MinI` | Missing |

### Minimap

| Native | Status |
|---|---|
| `MinimapPing` | Placeholder candidate |

### Mod

| Native | Status |
|---|---|
| `ModI` | Missing |

### Movie

| Native | Status |
|---|---|
| `MovieStartRecording` | Missing |
| `MovieStopRecording` | Missing |

### Order

| Native | Status |
|---|---|
| `Order` | Placeholder candidate |
| `OrderTargetingUnit` | Placeholder candidate |

### Perf

| Native | Status |
|---|---|
| `PerfTestGetFPS` | Missing |
| `PerfTestStart` | Missing |
| `PerfTestStop` | Missing |

### Ping

| Native | Status |
|---|---|
| `PingCreate` | Placeholder candidate |
| `PingDestroy` | Placeholder candidate |
| `PingLastCreated` | Placeholder candidate |
| `PingSetScale` | Placeholder candidate |
| `PingSetTooltip` | Placeholder candidate |

### Player

| Native | Status |
|---|---|
| `PlayerDifficulty` | Placeholder candidate |
| `PlayerGetColorIndex` | Missing |
| `PlayerGroupAdd` | Placeholder candidate |
| `PlayerGroupAll` | Placeholder candidate |
| `PlayerGroupCopy` | Placeholder candidate |
| `PlayerGroupEmpty` | Placeholder candidate |
| `PlayerGroupHasPlayer` | Placeholder candidate |
| `PlayerGroupRemove` | Placeholder candidate |
| `PlayerGroupSingle` | Placeholder candidate |
| `PlayerModifyPropertyInt` | Placeholder candidate |
| `PlayerName` | Missing |
| `PlayerPauseAllCharges` | Placeholder candidate |
| `PlayerPauseAllCooldowns` | Placeholder candidate |
| `PlayerScoreValueEnableAll` | Placeholder candidate |
| `PlayerSetAlliance` | Placeholder candidate |
| `PlayerSetState` | Placeholder candidate |
| `PlayerStatus` | Missing |
| `PlayerType` | Placeholder candidate |

### Point

| Native | Status |
|---|---|
| `PointSetFacing` | Placeholder candidate |

### Portrait

| Native | Status |
|---|---|
| `PortraitCreate` | Missing |
| `PortraitLastCreated` | Missing |
| `PortraitSetBorderVisible` | Missing |
| `PortraitSetFullscreen` | Missing |
| `PortraitSetVisible` | Missing |
| `PortraitUseTransition` | Missing |

### Preload

| Native | Status |
|---|---|
| `PreloadObject` | Placeholder candidate |

### Purchase

| Native | Status |
|---|---|
| `PurchaseCategoryCreate` | Missing |
| `PurchaseCategoryLastCreated` | Missing |
| `PurchaseCategorySetNameText` | Missing |
| `PurchaseCategorySetState` | Missing |
| `PurchaseGetSelectedPurchaseCategory` | Missing |
| `PurchaseGetSelectedPurchaseItem` | Missing |
| `PurchaseGroupCreate` | Missing |
| `PurchaseGroupLastCreated` | Missing |
| `PurchaseGroupSetIconFilePath` | Missing |
| `PurchaseGroupSetNameText` | Missing |
| `PurchaseGroupSetSlot` | Missing |
| `PurchaseGroupSetState` | Missing |
| `PurchaseGroupSetUnitLink` | Missing |
| `PurchaseItemCreate` | Missing |
| `PurchaseItemLastCreated` | Missing |
| `PurchaseItemPurchase` | Missing |
| `PurchaseItemSetCost` | Missing |
| `PurchaseItemSetDescriptionText` | Missing |
| `PurchaseItemSetIconFilePath` | Missing |
| `PurchaseItemSetMovieFilePath` | Missing |
| `PurchaseItemSetNameText` | Missing |
| `PurchaseItemSetState` | Missing |
| `PurchaseItemSetTooltipText` | Missing |
| `PurchaseSetSelectedPurchaseCategory` | Missing |

### Region

| Native | Status |
|---|---|
| `RegionCircle` | Placeholder candidate |
| `RegionContainsPoint` | Placeholder candidate |
| `RegionEntireMap` | Placeholder candidate |
| `RegionFromId` | Placeholder candidate |
| `RegionGetBoundsMax` | Placeholder candidate |
| `RegionGetBoundsMin` | Placeholder candidate |
| `RegionGetCenter` | Placeholder candidate |
| `RegionPlayableMap` | Placeholder candidate |
| `RegionRandomPoint` | Placeholder candidate |

### Research

| Native | Status |
|---|---|
| `ResearchCategoryCreate` | Missing |
| `ResearchCategoryLastCreated` | Missing |
| `ResearchCategorySetCurrentLevel` | Missing |
| `ResearchCategorySetLastLevel` | Missing |
| `ResearchCategorySetNameText` | Missing |
| `ResearchItemCreate` | Missing |
| `ResearchItemGetSelected` | Missing |
| `ResearchItemLastCreated` | Missing |
| `ResearchItemPurchase` | Missing |
| `ResearchItemSetConfirmationText` | Missing |
| `ResearchItemSetDescriptionText` | Missing |
| `ResearchItemSetIconFilePath` | Missing |
| `ResearchItemSetMovieFilePath` | Missing |
| `ResearchItemSetNameText` | Missing |
| `ResearchItemSetState` | Missing |
| `ResearchItemSetTooltipText` | Missing |
| `ResearchTierCreate` | Missing |
| `ResearchTierLastCreated` | Missing |
| `ResearchTierSetMaxPurchasesAllowed` | Missing |
| `ResearchTierSetRequiredLevel` | Missing |

### Set

| Native | Status |
|---|---|
| `SetNextMissionDifficulty` | Missing |

### Sound

| Native | Status |
|---|---|
| `SoundChannelSetVolume` | Placeholder candidate |
| `SoundLastPlayed` | Missing |
| `SoundPlay` | Placeholder candidate |
| `SoundPlayAtPoint` | Placeholder candidate |
| `SoundPlayOnUnit` | Placeholder candidate |
| `SoundStop` | Placeholder candidate |
| `SoundStopAllTriggerSounds` | Missing |

### Soundtrack

| Native | Status |
|---|---|
| `SoundtrackDefault` | Placeholder candidate |
| `SoundtrackPause` | Placeholder candidate |
| `SoundtrackPlay` | Placeholder candidate |
| `SoundtrackStop` | Missing |

### String

| Native | Status |
|---|---|
| `StringLength` | Missing |
| `StringReplaceWord` | Placeholder candidate |
| `StringSub` | Placeholder candidate |
| `StringToFixed` | Missing |
| `StringToText` | Placeholder candidate |

### Tech

| Native | Status |
|---|---|
| `TechTreeAbilityAllow` | Placeholder candidate |
| `TechTreeAbilityIsAllowed` | Placeholder candidate |
| `TechTreeRestrictionsEnable` | Placeholder candidate |
| `TechTreeUnitAllow` | Missing |
| `TechTreeUnitHelp` | Placeholder candidate |
| `TechTreeUnitHelpDefault` | Placeholder candidate |
| `TechTreeUpgradeAddLevel` | Placeholder candidate |
| `TechTreeUpgradeAllow` | Missing |
| `TechTreeUpgradeCount` | Placeholder candidate |

### Text

| Native | Status |
|---|---|
| `TextCase` | Placeholder candidate |
| `TextTagAttachToUnit` | Missing |
| `TextTagCreate` | Missing |
| `TextTagDestroy` | Missing |
| `TextTagLastCreated` | Missing |
| `TextTagSetAlignment` | Missing |
| `TextTagSetBackgroundBorderSize` | Missing |
| `TextTagSetBackgroundImage` | Missing |
| `TextTagSetColor` | Missing |
| `TextTagSetMaxSize` | Missing |
| `TextTagSetTextShadow` | Missing |
| `TextTagSetTime` | Missing |
| `TextTagShow` | Missing |
| `TextTagShowBackground` | Missing |
| `TextWithColor` | Missing |

### Timer

| Native | Status |
|---|---|
| `TimerCreate` | Missing |
| `TimerGetElapsed` | Missing |
| `TimerPause` | Placeholder candidate |
| `TimerStart` | Missing |

### Transmission

| Native | Status |
|---|---|
| `TransmissionClear` | Placeholder candidate |
| `TransmissionClearAll` | Placeholder candidate |
| `TransmissionLastSent` | Placeholder candidate |
| `TransmissionSetOption` | Placeholder candidate |
| `TransmissionSource` | Placeholder candidate |
| `TransmissionSourceFromModel` | Placeholder candidate |
| `TransmissionSourceFromMovie` | Missing |
| `TransmissionSourceFromUnit` | Placeholder candidate |

### Trigger

| Native | Status |
|---|---|
| `TriggerAddEventAbortMission` | Missing |
| `TriggerAddEventBattleReportPanelExit` | Missing |
| `TriggerAddEventBattleReportPanelPlayMission` | Missing |
| `TriggerAddEventBattleReportPanelPlayScene` | Missing |
| `TriggerAddEventChatMessage` | Missing |
| `TriggerAddEventCheatUsed` | Missing |
| `TriggerAddEventDialogControl` | Missing |
| `TriggerAddEventMercenaryPanelExit` | Missing |
| `TriggerAddEventMercenaryPanelPurchase` | Missing |
| `TriggerAddEventMercenaryPanelSelectionChanged` | Missing |
| `TriggerAddEventPlayerLeft` | Placeholder candidate |
| `TriggerAddEventPurchaseExit` | Missing |
| `TriggerAddEventPurchaseMade` | Missing |
| `TriggerAddEventResearchPanelExit` | Missing |
| `TriggerAddEventResearchPanelPurchase` | Missing |
| `TriggerAddEventSelectedPurchaseCategoryChanged` | Missing |
| `TriggerAddEventSelectedPurchaseItemChanged` | Missing |
| `TriggerAddEventTimePeriodic` | Placeholder candidate |
| `TriggerAddEventUnitAttacked` | Placeholder candidate |
| `TriggerAddEventUnitCargo` | Placeholder candidate |
| `TriggerAddEventUnitDamaged` | Placeholder candidate |
| `TriggerAddEventUnitDied` | Placeholder candidate |
| `TriggerAddEventUnitOrder` | Placeholder candidate |
| `TriggerAddEventUnitProperty` | Missing |
| `TriggerAddEventUnitRange` | Placeholder candidate |
| `TriggerAddEventUnitRangePoint` | Placeholder candidate |
| `TriggerAddEventUnitRegion` | Placeholder candidate |
| `TriggerAddEventUnitSelected` | Missing |
| `TriggerAddEventVictoryPanelExit` | Missing |
| `TriggerAddEventVictoryPanelPlayMissionAgain` | Missing |
| `TriggerDebugOutput` | Placeholder candidate |
| `TriggerEnable` | Placeholder candidate |
| `TriggerGetCurrent` | Placeholder candidate |
| `TriggerGetExecCount` | Placeholder candidate |
| `TriggerIsEnabled` | Placeholder candidate |
| `TriggerQueueClear` | Placeholder candidate |
| `TriggerQueueEnter` | Placeholder candidate |
| `TriggerQueueExit` | Placeholder candidate |
| `TriggerQueueIsEmpty` | Placeholder candidate |
| `TriggerQueuePause` | Placeholder candidate |
| `TriggerSkippableBegin` | Placeholder candidate |
| `TriggerStop` | Placeholder candidate |

### UI

| Native | Status |
|---|---|
| `UIAlertPoint` | Placeholder candidate |
| `UIAlertUnit` | Placeholder candidate |
| `UIClearMessages` | Placeholder candidate |
| `UIDisplayMessage` | Missing |
| `UIFlyerHelperClearOverride` | Placeholder candidate |
| `UIFlyerHelperOverride` | Placeholder candidate |
| `UIFrameVisible` | Placeholder candidate |
| `UISetCursorVisible` | Placeholder candidate |
| `UISetFrameVisible` | Placeholder candidate |
| `UISetGameMenuItemVisible` | Placeholder candidate |
| `UISetMode` | Placeholder candidate |
| `UISetNextLoadingScreen` | Missing |
| `UISetNextLoadingScreenImageScale` | Missing |
| `UISetNextLoadingScreenTextPosition` | Missing |
| `UISetRestartLoadingScreen` | Placeholder candidate |
| `UISetWorldVisible` | Missing |

### Unit

| Native | Status |
|---|---|
| `UnitBehaviorAdd` | Placeholder candidate |
| `UnitBehaviorAddPlayer` | Missing |
| `UnitBehaviorRemove` | Placeholder candidate |
| `UnitBehaviorRemovePlayer` | Missing |
| `UnitCargoLastCreatedGroup` | Placeholder candidate |
| `UnitCargoValue` | Missing |
| `UnitClearSelection` | Placeholder candidate |
| `UnitFilter` | Placeholder candidate |
| `UnitFilterMatch` | Placeholder candidate |
| `UnitFromId` | Placeholder candidate |
| `UnitGroup` | Placeholder candidate |
| `UnitGroupCopy` | Placeholder candidate |
| `UnitGroupEmpty` | Placeholder candidate |
| `UnitGroupFilter` | Placeholder candidate |
| `UnitGroupFilterPlayer` | Placeholder candidate |
| `UnitGroupHasUnit` | Placeholder candidate |
| `UnitGroupIssueOrder` | Placeholder candidate |
| `UnitGroupLoopBegin` | Placeholder candidate |
| `UnitGroupLoopCurrent` | Placeholder candidate |
| `UnitGroupLoopDone` | Placeholder candidate |
| `UnitGroupLoopEnd` | Placeholder candidate |
| `UnitGroupLoopStep` | Placeholder candidate |
| `UnitGroupRandomUnit` | Placeholder candidate |
| `UnitGroupRemove` | Placeholder candidate |
| `UnitGroupSelect` | Missing |
| `UnitGroupSelected` | Missing |
| `UnitGroupUnit` | Placeholder candidate |
| `UnitKill` | Placeholder candidate |
| `UnitLastCreatedGroup` | Placeholder candidate |
| `UnitPauseAll` | Placeholder candidate |
| `UnitRemove` | Placeholder candidate |
| `UnitSetInfoText` | Placeholder candidate |
| `UnitSetOwner` | Placeholder candidate |
| `UnitSetPropertyFixed` | Placeholder candidate |
| `UnitSetScale` | Placeholder candidate |
| `UnitSetState` | Placeholder candidate |
| `UnitSetTeamColorIndex` | Placeholder candidate |
| `UnitTypeFromString` | Placeholder candidate |
| `UnitTypeGetName` | Placeholder candidate |

### Victory

| Native | Status |
|---|---|
| `VictoryPanelAddAchievement` | Placeholder candidate |
| `VictoryPanelAddCustomStatisticLine` | Placeholder candidate |
| `VictoryPanelAddTrackedStatistic` | Placeholder candidate |
| `VictoryPanelSetAchievementsTitle` | Missing |
| `VictoryPanelSetBackgroundFilePath` | Missing |
| `VictoryPanelSetMissionText` | Missing |
| `VictoryPanelSetMissionTimeText` | Missing |
| `VictoryPanelSetMissionTimeTitle` | Missing |
| `VictoryPanelSetMissionTitle` | Missing |
| `VictoryPanelSetRewardCredits` | Missing |
| `VictoryPanelSetRewardTitle` | Missing |
| `VictoryPanelSetStatisticsTitle` | Missing |
| `VictoryPanelSetSummaryBackgroundFilePath` | Missing |
| `VictoryPanelSetVictoryText` | Missing |

### Vis

| Native | Status |
|---|---|
| `VisEnable` | Placeholder candidate |
| `VisExploreArea` | Placeholder candidate |
| `VisRevealArea` | Placeholder candidate |
| `VisRevealerCreate` | Placeholder candidate |
| `VisRevealerDestroy` | Placeholder candidate |
| `VisRevealerEnable` | Missing |
| `VisRevealerLastCreated` | Placeholder candidate |
