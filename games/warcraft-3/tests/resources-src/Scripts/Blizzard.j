// Minimal Blizzard.j for WC3 in-engine test fixtures.
// Keep helpers only when an engine regression test needs the stock wrapper semantics.

globals
    constant integer bj_CAMPAIGN_INDEX_H = 1
    constant integer bj_CAMPAIGN_OFFSET_H = 1
    constant integer bj_MISSION_INDEX_H00 = bj_CAMPAIGN_OFFSET_H * 1000 + 0
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
