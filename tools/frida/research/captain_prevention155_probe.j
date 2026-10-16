globals
    unit array udg_RsgUnit
    unit udg_RsgCaster=null
    integer udg_RsgTick=0
    timer udg_RsgTimer=null
endglobals
function RsgMark takes string s returns nothing
    call Preload("RSG tick="+I2S(udg_RsgTick)+" "+s)
endfunction
function RsgCast takes integer abil, integer index returns nothing
    call RsgMark("label=cast code="+I2S(abil)+" target="+I2S(index)+" accepted="+I2S(IntegerTertiaryOp(IssueTargetOrderById(udg_RsgCaster,OrderId("drunkenhaze"),udg_RsgUnit[index]),1,0)))
endfunction
function RsgCastCode takes integer abil, integer index returns nothing
    if udg_RsgCaster!=null then
        call RemoveUnit(udg_RsgCaster)
    endif
    set udg_RsgCaster=CreateUnit(Player(0),'hRC0',GetUnitX(udg_RsgUnit[index]),GetUnitY(udg_RsgUnit[index])-160.0,90.0)
    call UnitAddAbility(udg_RsgCaster,abil)
    call RsgCast(abil,index)
endfunction
function RsgTick takes nothing returns nothing
    local integer i=0
    set udg_RsgTick=udg_RsgTick+1
    if udg_RsgTick==2 or udg_RsgTick==20 then
        call RsgCastCode('APM1',1)
    elseif udg_RsgTick==40 then
        call RsgCastCode('APM2',1)
    elseif udg_RsgTick==60 or udg_RsgTick==140 or udg_RsgTick==180 then
        call RsgMark("label=remove-buff result="+I2S(IntegerTertiaryOp(UnitRemoveAbility(udg_RsgUnit[1],'BNdh'),1,0)))
    elseif udg_RsgTick==80 then
        call RsgMark("label=remove-attack result="+I2S(IntegerTertiaryOp(UnitRemoveAbility(udg_RsgUnit[1],'Aatk'),1,0)))
    elseif udg_RsgTick==100 or udg_RsgTick==160 then
        call RsgCastCode('APM1',1)
    elseif udg_RsgTick==120 then
        call RsgMark("label=add-attack result="+I2S(IntegerTertiaryOp(UnitAddAbility(udg_RsgUnit[1],'Aatk'),1,0)))
    elseif udg_RsgTick==200 then
        call RsgCastCode('APMS',0)
    elseif udg_RsgTick==240 then
        call RsgCastCode('APM8',2)
    elseif udg_RsgTick==260 then
        call RsgMark("label=remove-spell-buff result="+I2S(IntegerTertiaryOp(UnitRemoveAbility(udg_RsgUnit[2],'BNdh'),1,0)))
    endif
    if ModuloInteger(udg_RsgTick,10)==0 then
        loop
            exitwhen i==3
            call RsgMark("label=sample u="+I2S(i)+" x="+R2S(GetUnitX(udg_RsgUnit[i]))+" y="+R2S(GetUnitY(udg_RsgUnit[i]))+" o="+I2S(GetUnitCurrentOrder(udg_RsgUnit[i]))+" buff="+I2S(GetUnitAbilityLevel(udg_RsgUnit[i],'BNdh')))
            set i=i+1
        endloop
    endif
    if udg_RsgTick==280 then
        call RsgMark("label=complete")
        call TimerStart(udg_RsgTimer,100000.0,false,null)
        call PreloadGenEnd("rs-group0346212.txt")
    endif
endfunction
function PathProbeInit takes nothing returns nothing
    call PreloadGenStart()
    call RsgMark("label=start_prevention155")
    set udg_RsgUnit[0]=CreateUnit(Player(0),'hRA0',-1936.0,-976.0,90.0)
    set udg_RsgUnit[1]=CreateUnit(Player(0),'hRA1',-1656.0,-976.0,90.0)
    set udg_RsgUnit[2]=CreateUnit(Player(0),'hRA2',-1376.0,-976.0,90.0)
    call RsgMark("label=created")
    set udg_RsgTimer=CreateTimer()
    call TimerStart(udg_RsgTimer,0.1,true,function RsgTick)
endfunction
