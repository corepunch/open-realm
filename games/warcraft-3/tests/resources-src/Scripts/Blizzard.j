// Minimal Blizzard.j for WC3 in-engine test fixtures.
// Keep helpers only when an engine regression test needs the stock wrapper semantics.

globals
    constant integer bj_CAMPAIGN_INDEX_H = 1
    constant integer bj_CAMPAIGN_OFFSET_H = 1
    constant integer bj_MISSION_INDEX_H00 = bj_CAMPAIGN_OFFSET_H * 1000 + 0
    boolean array bj_stockAllowedPermanent
    boolean array bj_stockAllowedCharged
    boolean array bj_stockAllowedArtifact
endglobals

// The campaign-progress tests do not inspect the selected race; retain the call
// boundary used by stock SetCampaignAvailableBJ without duplicating race globals.
function SetCampaignMenuRaceBJ takes integer campaignNumber returns nothing
endfunction

function SetMissionAvailableBJ takes boolean available, integer missionIndex returns nothing
    local integer campaignNumber = missionIndex / 1000
    local integer missionNumber = missionIndex - campaignNumber * 1000
    call SetMissionAvailable(campaignNumber, missionNumber, available)
endfunction

function SetCampaignAvailableBJ takes boolean available, integer campaignNumber returns nothing
    local integer campaignOffset

    if (campaignNumber == bj_CAMPAIGN_INDEX_H) then
        call SetTutorialCleared(true)
    endif

    set campaignOffset = campaignNumber
    call SetCampaignAvailable(campaignOffset, available)
    call SetCampaignMenuRaceBJ(campaignNumber)
    call ForceCampaignSelectScreen()
endfunction

function IsUnitAliveBJ takes unit whichUnit returns boolean
    return GetWidgetLife(whichUnit) > 0.405
endfunction

function SetPlayerHandicapXPBJ takes player whichPlayer, real handicapPercent returns nothing
    call SetPlayerHandicapXP(whichPlayer, handicapPercent * 0.01)
endfunction

function GetPlayerHandicapXPBJ takes player whichPlayer returns real
    return GetPlayerHandicapXP(whichPlayer) * 100
endfunction

// Item drops are Blizzard.j script helpers, not native inventory drops. Keep
// their RNG, metadata, and stock update ordering identical to retail code.
function UpdateStockAvailability takes item whichItem returns nothing
    local itemtype iType = GetItemType(whichItem)
    local integer iLevel = GetItemLevel(whichItem)

    if (iType == ITEM_TYPE_PERMANENT) then
        set bj_stockAllowedPermanent[iLevel] = true
    elseif (iType == ITEM_TYPE_CHARGED) then
        set bj_stockAllowedCharged[iLevel] = true
    elseif (iType == ITEM_TYPE_ARTIFACT) then
        set bj_stockAllowedArtifact[iLevel] = true
    endif
endfunction

function UnitDropItem takes unit inUnit, integer inItemID returns item
    local real x
    local real y
    local real radius = 32.0
    local real unitX
    local real unitY
    local item droppedItem

    if (inItemID == -1) then
        return null
    endif
    set unitX = GetUnitX(inUnit)
    set unitY = GetUnitY(inUnit)
    set x = GetRandomReal(unitX - radius, unitX + radius)
    set y = GetRandomReal(unitY - radius, unitY + radius)
    set droppedItem = CreateItem(inItemID, x, y)
    call SetItemDropID(droppedItem, GetUnitTypeId(inUnit))
    call UpdateStockAvailability(droppedItem)
    return droppedItem
endfunction

function WidgetDropItem takes widget inWidget, integer inItemID returns item
    local real x
    local real y
    local real radius = 32.0
    local real widgetX
    local real widgetY

    if (inItemID == -1) then
        return null
    endif
    set widgetX = GetWidgetX(inWidget)
    set widgetY = GetWidgetY(inWidget)
    set x = GetRandomReal(widgetX - radius, widgetX + radius)
    set y = GetRandomReal(widgetY - radius, widgetY + radius)
    return CreateItem(inItemID, x, y)
endfunction
