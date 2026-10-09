// GROUP-03.4.7.3 research probe: autonomous attack-captain home/retreat/goal policies.
// The map timer only creates/removes Footmen and sends CommandAI(Player(0),phase,0); the AI script
// (GROUP-03.4.7.3_probe.ai) executes every public captain native.  Markers: Preload("RSH ...").
globals
    unit array udg_RshUnit
    integer udg_RshTick=0
    integer udg_RshCount=0
    timer udg_RshTimer=null
endglobals
function RshMark takes string s returns nothing
    call Preload("RSH tick="+I2S(udg_RshTick)+" "+s)
endfunction
function RshSample takes nothing returns nothing
    local integer i=0
    loop
        exitwhen i==udg_RshCount
        if udg_RshUnit[i]!=null then
            call RshMark("label=sample u="+I2S(i)+" x="+R2S(GetUnitX(udg_RshUnit[i]))+" y="+R2S(GetUnitY(udg_RshUnit[i]))+" o="+I2S(GetUnitCurrentOrder(udg_RshUnit[i])))
        endif
        set i=i+1
    endloop
endfunction
function RshCommand takes integer phase returns nothing
    call RshMark("label=command phase="+I2S(phase))
    call CommandAI(Player(0),phase,0)
endfunction
function RshRemove takes integer first,integer last returns nothing
    local integer i=first
    loop
        exitwhen i>last
        if udg_RshUnit[i]!=null then
            call RemoveUnit(udg_RshUnit[i])
            set udg_RshUnit[i]=null
        endif
        set i=i+1
    endloop
    call RshMark("label=removed first="+I2S(first)+" last="+I2S(last))
endfunction
// Public SetUnitState lowers members to 42 life (10 percent of Footman 420) for the healthy-count policy.
function RshHurt takes integer first,integer last returns nothing
    local integer i=first
    loop
        exitwhen i>last
        if udg_RshUnit[i]!=null then
            call SetUnitState(udg_RshUnit[i],UNIT_STATE_LIFE,42.0)
        endif
        set i=i+1
    endloop
    call RshMark("label=hurt first="+I2S(first)+" last="+I2S(last))
endfunction
function RshTick takes nothing returns nothing
    set udg_RshTick=udg_RshTick+1
    if udg_RshTick==9 then
        call RshMark("label=start-ai")
        call StartCampaignAI(Player(0),"Scripts\\wc3_captain_probe.ai")
@SCHEDULE@
    elseif udg_RshTick==@END_TICK@ then
        call RshSample()
        call RshMark("label=complete")
        call PreloadGenEnd("rs-group0473.txt")
        call PauseTimer(udg_RshTimer)
        return
    endif
    if ModuloInteger(udg_RshTick,5)==0 then
        call RshSample()
    endif
endfunction
function PathProbeInit takes nothing returns nothing
    local integer i=0
    call FogEnable(false)
    call FogMaskEnable(false)
    call PreloadGenClear()
    call Preload("RSH tick=0 label=start_group0473 variant=@VARIANT@")
    loop
        exitwhen i==@MEMBERS@
        set udg_RshUnit[i]=CreateUnit(Player(0),'hfoo',-1936.0+I2R(ModuloInteger(i,4))*80.0,-976.0-I2R(i/4)*80.0,90.0)
        call RshMark("label=birth u="+I2S(i))
        set i=i+1
    endloop
    set udg_RshCount=@MEMBERS@
    call SetCameraPosition(-1856.0,-512.0)
    set udg_RshTimer=CreateTimer()
    call TimerStart(udg_RshTimer,0.1,true,function RshTick)
endfunction
