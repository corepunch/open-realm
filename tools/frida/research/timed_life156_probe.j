globals
    unit array udg_RsgUnit
    integer array udg_RsgBuff
    integer udg_RsgTick=0
    timer udg_RsgTimer=null
endglobals
function RsgMark takes string s returns nothing
    call Preload("RSG tick="+I2S(udg_RsgTick)+" "+s)
endfunction
function RsgSample takes integer i returns nothing
    call RsgMark("label=sample u="+I2S(i)+" life="+R2S(GetWidgetLife(udg_RsgUnit[i]))+" summoned="+I2S(IntegerTertiaryOp(IsUnitType(udg_RsgUnit[i],UNIT_TYPE_SUMMONED),1,0))+" ownbuff="+I2S(GetUnitAbilityLevel(udg_RsgUnit[i],udg_RsgBuff[i]))+" btlf="+I2S(GetUnitAbilityLevel(udg_RsgUnit[i],'BTLF'))+" food="+I2S(GetPlayerState(Player(0),PLAYER_STATE_RESOURCE_FOOD_USED)))
endfunction
function RsgTick takes nothing returns nothing
    local integer i=0
    set udg_RsgTick=udg_RsgTick+1
    if udg_RsgTick==5 then
        call RsgMark("label=refresh")
        call UnitApplyTimedLife(udg_RsgUnit[12],'BTLF',3.0)
        call UnitPauseTimedLife(udg_RsgUnit[13],true)
        call RsgMark("label=remove result="+I2S(IntegerTertiaryOp(UnitRemoveAbility(udg_RsgUnit[14],'BTLF'),1,0)))
        call UnitApplyTimedLife(udg_RsgUnit[15],'BHwe',3.0)
    elseif udg_RsgTick==15 then
        call UnitPauseTimedLife(udg_RsgUnit[13],false)
        call RsgMark("label=resume")
    endif
    loop
        exitwhen i==18
        call RsgSample(i)
        set i=i+1
    endloop
    if udg_RsgTick==60 then
        call RsgMark("label=complete")
        call TimerStart(udg_RsgTimer,100000.0,false,null)
        call PreloadGenEnd("rs-timedlife156.txt")
    endif
endfunction
function PathProbeInit takes nothing returns nothing
    local integer i=0
    local real duration=2.0
    call PreloadGenStart()
    call RsgMark("label=start_timedlife156")
    set udg_RsgBuff[0]='BTLF'
    set udg_RsgBuff[1]='BUan'
    set udg_RsgBuff[2]='BFig'
    set udg_RsgBuff[3]='BEfn'
    set udg_RsgBuff[4]='Bhwd'
    set udg_RsgBuff[5]='Bplg'
    set udg_RsgBuff[6]='Brai'
    set udg_RsgBuff[7]='BHwe'
    set udg_RsgBuff[8]='ZZZZ'
    loop
        exitwhen i==18
        if i>=9 then
            set udg_RsgBuff[i]='BTLF'
        endif
        set udg_RsgUnit[i]=CreateUnit(Player(0),'hRA0',-1936.0+I2R(ModuloInteger(i,6))*128.0,-976.0-I2R(i/6)*128.0,90.0)
        set duration=2.0
        if i==9 then
            set duration=0.0
        elseif i==10 then
            set duration=-1.0
        elseif i==11 then
            set duration=0.01
        elseif i>=12 and i<=15 then
            set duration=1.0
        endif
        if i==15 then
            set udg_RsgBuff[i]='BUan'
        elseif i==16 then
            call SetUnitUseFood(udg_RsgUnit[i],false)
        elseif i==17 then
            call KillUnit(udg_RsgUnit[i])
        endif
        call RsgMark("label=apply u="+I2S(i)+" buff="+I2S(udg_RsgBuff[i])+" duration="+R2S(duration))
        call UnitApplyTimedLife(udg_RsgUnit[i],udg_RsgBuff[i],duration)
        call RsgSample(i)
        set i=i+1
    endloop
    call UnitApplyTimedLife(null,'BTLF',1.0)
    set udg_RsgTimer=CreateTimer()
    call TimerStart(udg_RsgTimer,0.1,true,function RsgTick)
endfunction
