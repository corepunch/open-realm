// GROUP-03.4.6.2.1.2 research probe: public producers of the private Captain approach range.
// Recruits are Player(0) units created before StartCampaignAI; every state change below uses an
// ordinary public JASS native or authored object data.  Markers: Preload("RSG ...").
globals
    unit array udg_RsgUnit
    unit array udg_RsgCaster
    integer udg_RsgTick=0
    integer udg_RsgCount=0
    timer udg_RsgTimer=null
endglobals
function RsgMark takes string s returns nothing
    call Preload("RSG tick="+I2S(udg_RsgTick)+" "+s)
endfunction
function RsgSample takes nothing returns nothing
    local integer i=0
    loop
        exitwhen i==udg_RsgCount
        if udg_RsgUnit[i]!=null then
            call RsgMark("label=sample u="+I2S(i)+" x="+R2S(GetUnitX(udg_RsgUnit[i]))+" y="+R2S(GetUnitY(udg_RsgUnit[i]))+" o="+I2S(GetUnitCurrentOrder(udg_RsgUnit[i]))+" ill="+I2S(IntegerTertiaryOp(IsUnitIllusion(udg_RsgUnit[i]),1,0))+" hp="+R2S(GetUnitState(udg_RsgUnit[i],UNIT_STATE_LIFE)))
        endif
        set i=i+1
    endloop
endfunction
// Drunken Haze based custom abilities carry only the authored Attacks Prevented mask.
function RsgHaze takes integer slot,integer abil,integer target returns nothing
    set udg_RsgCaster[slot]=CreateUnit(Player(0),'hRC0',GetUnitX(udg_RsgUnit[target]),GetUnitY(udg_RsgUnit[target])-160.0,90.0)
    call UnitAddAbility(udg_RsgCaster[slot],abil)
    call RsgMark("label=haze caster="+I2S(slot)+" ability="+I2S(abil)+" target="+I2S(target)+" issued="+I2S(IntegerTertiaryOp(IssueTargetOrder(udg_RsgCaster[slot],"drunkenhaze",udg_RsgUnit[target]),1,0)))
endfunction
// A Hero caster has a native inventory; the Wand of Illusion item is used through the public item native.
function RsgWand takes integer slot,integer target returns nothing
    local item it
    set udg_RsgCaster[slot]=CreateUnit(Player(0),'HRC0',GetUnitX(udg_RsgUnit[target]),GetUnitY(udg_RsgUnit[target])-160.0,90.0)
    set it=UnitAddItemById(udg_RsgCaster[slot],'will')
    call RsgMark("label=wand caster="+I2S(slot)+" target="+I2S(target)+" item="+I2S(GetItemTypeId(it))+" slots="+I2S(UnitInventorySize(udg_RsgCaster[slot]))+" held="+I2S(IntegerTertiaryOp(UnitHasItem(udg_RsgCaster[slot],it),1,0))+" used="+I2S(IntegerTertiaryOp(UnitUseItemTarget(udg_RsgCaster[slot],it,udg_RsgUnit[target]),1,0)))
    set it=null
endfunction
function RsgCollectIllusions takes nothing returns nothing
    local group g=CreateGroup()
    local unit u
    call GroupEnumUnitsOfPlayer(g,Player(0),null)
    loop
        set u=FirstOfGroup(g)
        exitwhen u==null
        call GroupRemoveUnit(g,u)
        if IsUnitIllusion(u) then
            set udg_RsgUnit[udg_RsgCount]=u
            call RsgMark("label=illusion u="+I2S(udg_RsgCount)+" type="+I2S(GetUnitTypeId(u)))
            set udg_RsgCount=udg_RsgCount+1
        endif
    endloop
    call DestroyGroup(g)
    set g=null
endfunction
function RsgRemoveCasters takes nothing returns nothing
    local integer i=0
    loop
        exitwhen i==12
        if udg_RsgCaster[i]!=null then
            call RemoveUnit(udg_RsgCaster[i])
            set udg_RsgCaster[i]=null
        endif
        set i=i+1
    endloop
endfunction
function RsgTick takes nothing returns nothing
    set udg_RsgTick=udg_RsgTick+1
    if udg_RsgTick==2 then
        call RsgMark("label=pre-admission")
        call RsgHaze(0,'ARD1',1)
        call RsgHaze(1,'ARD2',2)
        call RsgHaze(2,'ARD1',3)
        call RsgHaze(3,'ARD4',10)
        call RsgHaze(4,'ARD1',11)
        call UnitApplyTimedLife(udg_RsgUnit[5],'BTLF',600.0)
        call RsgMark("label=removed-attack u=7 result="+I2S(IntegerTertiaryOp(UnitRemoveAbility(udg_RsgUnit[7],'Aatk'),1,0)))
    elseif udg_RsgTick==9 then
        call RsgRemoveCasters()
        call RsgMark("label=start-ai")
        call StartCampaignAI(Player(0),"Scripts\\wc3_captain_probe.ai")
    elseif udg_RsgTick==40 then
        call RsgMark("label=illusions")
        call RsgWand(5,0)
        call RsgWand(6,12)
    elseif udg_RsgTick==55 then
        call RsgRemoveCasters()
        call RsgCollectIllusions()
    // One member at a time leaves the outer captain circle through public SetUnitPosition (110-tick spacing
    // lets the previous member re-enter the inner circle).  With a single missing inner member original
    // 9d8eb0 reissues that member's private approach (fresh 9d86f0); a public change applied ten ticks
    // later, during the private walk back, must leave the stored physical range untouched.
    elseif udg_RsgTick==90 then
        call RsgMark("label=depart-1 u=4")
        call SetUnitPosition(udg_RsgUnit[4],-2000.0,-1744.0)
    elseif udg_RsgTick==100 then
        call RsgMark("label=change u=4 ranged-prevented")
        call RsgHaze(7,'ARD2',4)
    elseif udg_RsgTick==108 then
        call RsgRemoveCasters()
    elseif udg_RsgTick==200 then
        call RsgMark("label=depart-1 u=6")
        call SetUnitPosition(udg_RsgUnit[6],-2000.0,-1744.0)
    elseif udg_RsgTick==210 then
        call RsgMark("label=change u=6 timed-life")
        call UnitApplyTimedLife(udg_RsgUnit[6],'BTLF',600.0)
    elseif udg_RsgTick==310 then
        call RsgMark("label=depart-1 u=8")
        call SetUnitPosition(udg_RsgUnit[8],-2000.0,-1744.0)
    elseif udg_RsgTick==320 then
        call RsgMark("label=change u=8 removed-attack")
        call RsgMark("label=removed-attack u=8 result="+I2S(IntegerTertiaryOp(UnitRemoveAbility(udg_RsgUnit[8],'Aatk'),1,0)))
    elseif udg_RsgTick==420 then
        call RsgMark("label=depart-1 u=9")
        call SetUnitPosition(udg_RsgUnit[9],-2000.0,-1744.0)
    elseif udg_RsgTick==430 then
        call RsgMark("label=change u=9 long-rifles")
        call SetPlayerTechResearched(Player(0),'Rhri',1)
    elseif udg_RsgTick==530 then
        call RsgMark("label=depart-2 u=4")
        call SetUnitPosition(udg_RsgUnit[4],-2000.0,-1744.0)
    elseif udg_RsgTick==640 then
        call RsgMark("label=depart-2 u=6")
        call SetUnitPosition(udg_RsgUnit[6],-2000.0,-1744.0)
    elseif udg_RsgTick==750 then
        call RsgMark("label=depart-2 u=8")
        call SetUnitPosition(udg_RsgUnit[8],-2000.0,-1744.0)
    elseif udg_RsgTick==860 then
        call RsgMark("label=depart-2 u=9")
        call SetUnitPosition(udg_RsgUnit[9],-2000.0,-1744.0)
    elseif udg_RsgTick==970 then
        call RsgMark("label=depart u=12 hero")
        call SetUnitPosition(udg_RsgUnit[12],-2000.0,-1744.0)
    elseif udg_RsgTick==1080 then
        call RsgSample()
        call RsgMark("label=complete")
        call PreloadGenEnd("rs-group0346212.txt")
        call PauseTimer(udg_RsgTimer)
        return
    endif
    if ModuloInteger(udg_RsgTick,10)==0 then
        call RsgSample()
    endif
endfunction
function PathProbeInit takes nothing returns nothing
    local integer i=0
    local integer array kind
    call FogEnable(false)
    call FogMaskEnable(false)
    call PreloadGenClear()
    // 0 control, 1 melee-prevented, 2 missile ranged-prevented, 3 missile melee-prevented,
    // 4 missile post-admission ranged-prevented, 5 timed life, 6 post timed life, 7 no Attack,
    // 8 post no Attack, 9 Rifleman upgrade, 10 special ranged-special-prevented, 11 special melee-prevented,
    // 12 Hero melee100, 13 Hero883, 14 Hero884.
    set kind[0]='hRA0'
    set kind[1]='hRA1'
    set kind[2]='hRA2'
    set kind[3]='hRA3'
    set kind[4]='hRA4'
    set kind[5]='hRA5'
    set kind[6]='hRA6'
    set kind[7]='hRA7'
    set kind[8]='hRA8'
    set kind[9]='hRA9'
    set kind[10]='hRAs'
    set kind[11]='hRAt'
    set kind[12]='HRA0'
    set kind[13]='HRA1'
    set kind[14]='HRA2'
    call Preload("RSG tick=0 label=start_group0346212")
    loop
        exitwhen i==15
        set udg_RsgUnit[i]=CreateUnit(Player(0),kind[i],-1936.0+I2R(ModuloInteger(i,4))*80.0,-976.0-I2R(i/4)*80.0,90.0)
        call RsgMark("label=birth u="+I2S(i)+" type="+I2S(kind[i]))
        set i=i+1
    endloop
    set udg_RsgCount=15
    call SetCameraPosition(-1856.0,-512.0)
    set udg_RsgTimer=CreateTimer()
    call TimerStart(udg_RsgTimer,0.1,true,function RsgTick)
endfunction
