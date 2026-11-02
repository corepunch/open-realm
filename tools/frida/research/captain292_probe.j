// StopGathering owns initial Town selection; preserve the active unit order.
globals
    unit array udg_R292Units
    integer udg_R292Tick=0
    timer udg_R292Timer=null
endglobals
function R292Tick takes nothing returns nothing
    local integer i=0
    set udg_R292Tick=udg_R292Tick+1
    if udg_R292Tick==5 then
        call Preload("R292 label=start-ai")
        call StartCampaignAI(Player(0),"Scripts\\captain292.ai")
    elseif udg_R292Tick==15 then
        call CommandAI(Player(0),1,0)
    elseif udg_R292Tick==25 then
        call Preload("R292 label=complete")
        call PreloadGenEnd("rs-captain292.txt")
        call PauseTimer(udg_R292Timer)
    endif
    loop
        exitwhen i==3
        call Preload("R292 tick="+I2S(udg_R292Tick)+" u="+I2S(i)+" x="+R2S(GetUnitX(udg_R292Units[i]))+" y="+R2S(GetUnitY(udg_R292Units[i]))+" o="+I2S(GetUnitCurrentOrder(udg_R292Units[i])))
        set i=i+1
    endloop
endfunction
function PathProbeInit takes nothing returns nothing
    call PreloadGenClear()
    call FogEnable(false)
    call FogMaskEnable(false)
    call Preload("R292 tick=0 label=start")
@UNITS@
    call IssuePointOrder(udg_R292Units[2],"move",-1552.0,-1424.0)
    set udg_R292Timer=CreateTimer()
    call TimerStart(udg_R292Timer,0.1,true,function R292Tick)
endfunction
