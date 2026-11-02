// One public lifetime: start fallback, authored override, hall, mine, movement/removal.
globals
    unit array udg_R291Units
    unit udg_R291Hall=null
    unit udg_R291Mine=null
    integer udg_R291Tick=0
    timer udg_R291Timer=null
endglobals
function R291Mark takes string value returns nothing
    call Preload("R291 tick="+I2S(udg_R291Tick)+" "+value)
endfunction
function R291Command takes integer phase returns nothing
    call R291Mark("label=command phase="+I2S(phase))
    call CommandAI(Player(0),phase,0)
endfunction
function R291Tick takes nothing returns nothing
    local integer i=0
    set udg_R291Tick=udg_R291Tick+1
    if udg_R291Tick==5 then
        call R291Mark("label=start-ai")
        call StartCampaignAI(Player(0),"Scripts\\captain291.ai")
    elseif udg_R291Tick==15 then
        call R291Command(1)
    elseif udg_R291Tick==25 then
        call R291Command(2)
    elseif udg_R291Tick==35 then
        call R291Mark("label=hall-create-before")
        if udg_R291Hall==null then
            set udg_R291Hall=CreateUnit(Player(0),'htow',-1936.0,-976.0,0.0)
        endif
        call R291Mark("label=hall-create-after x="+R2S(GetUnitX(udg_R291Hall))+" y="+R2S(GetUnitY(udg_R291Hall)))
        call R291Command(2)
    elseif udg_R291Tick==50 then
        call R291Mark("label=mine-create-before")
        if udg_R291Mine==null then
            set udg_R291Mine=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'ngol',-1568.0,-976.0,0.0)
        endif
        call R291Mark("label=mine-create-after x="+R2S(GetUnitX(udg_R291Mine))+" y="+R2S(GetUnitY(udg_R291Mine)))
        call R291Command(2)
    elseif udg_R291Tick==65 then
        call R291Mark("label=mine-move-before")
        call SetUnitX(udg_R291Mine,-1568.0)
        call SetUnitY(udg_R291Mine,-560.0)
        call R291Mark("label=mine-move-after x="+R2S(GetUnitX(udg_R291Mine))+" y="+R2S(GetUnitY(udg_R291Mine)))
        call R291Command(2)
    elseif udg_R291Tick==80 then
        call R291Mark("label=mine-remove-before")
        call RemoveUnit(udg_R291Mine)
        set udg_R291Mine=null
        call R291Mark("label=mine-remove-after")
        call R291Command(2)
    elseif udg_R291Tick==95 then
        call R291Mark("label=hall-remove-before")
        call RemoveUnit(udg_R291Hall)
        set udg_R291Hall=null
        call R291Mark("label=hall-remove-after")
        call R291Command(2)
    elseif udg_R291Tick==110 then
        call R291Command(3)
    elseif udg_R291Tick==125 then
        call R291Command(1)
    elseif udg_R291Tick==135 then
        call R291Command(2)
    elseif udg_R291Tick==145 then
        call R291Mark("label=complete")
        call PreloadGenEnd("rs-captain291.txt")
        call PauseTimer(udg_R291Timer)
        return
    endif
    if ModuloInteger(udg_R291Tick,5)==0 then
        loop
            exitwhen i==3
            call R291Mark("label=sample u="+I2S(i)+" x="+R2S(GetUnitX(udg_R291Units[i]))+" y="+R2S(GetUnitY(udg_R291Units[i]))+" o="+I2S(GetUnitCurrentOrder(udg_R291Units[i])))
            set i=i+1
        endloop
    endif
endfunction
function PathProbeInit takes nothing returns nothing
    local integer i=0
    call PreloadGenClear()
    call FogEnable(false)
    call FogMaskEnable(false)
    call Preload("R291 tick=0 label=start")
@ANCHORS@
    loop
        exitwhen i==3
        set udg_R291Units[i]=CreateUnit(Player(0),'hfoo',-2064.0+I2R(i)*80.0,-144.0,0.0)
        set i=i+1
    endloop
    set udg_R291Timer=CreateTimer()
    call TimerStart(udg_R291Timer,0.1,true,function R291Tick)
endfunction
