// Public temporary Captain policy transitions, user queues and physical motion.
globals
    unit array udg_RsgUnit
    integer udg_RsgTick=0
    timer udg_RsgTimer=null
endglobals
function RsgMark takes string s returns nothing
    call Preload("RSG tick="+I2S(udg_RsgTick)+" "+s)
endfunction
function RsgSample takes integer i,string caption returns nothing
    call RsgMark("label="+caption+" u="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_RsgUnit[i]))+" life="+R2S(GetWidgetLife(udg_RsgUnit[i]))+" x="+R2S(GetUnitX(udg_RsgUnit[i]))+" y="+R2S(GetUnitY(udg_RsgUnit[i])))
endfunction
function RsgApply takes integer i,integer buffcode returns nothing
    call RsgSample(i,"before")
    call UnitApplyTimedLife(udg_RsgUnit[i],buffcode,30.0)
    call RsgSample(i,"after")
endfunction
function RsgTick takes nothing returns nothing
    local integer i=0
    set udg_RsgTick=udg_RsgTick+1
    if udg_RsgTick==1 then
        call RsgMark("label=before-ai")
        call StartCampaignAI(Player(0),"Scripts\\wc3_enrollment158.ai")
        call RsgMark("label=after-ai")
    elseif udg_RsgTick==2 then
        call IssuePointOrder(udg_RsgUnit[0],"move",-1536.0,-400.0)
        call RsgApply(0,'BTLF')
        call IssuePointOrder(udg_RsgUnit[1],"move",-1408.0,-400.0)
        call RsgApply(1,'BHwe')
    elseif udg_RsgTick==3 then
        call IssuePointOrder(udg_RsgUnit[0],"move",-1536.0,-400.0)
        call RsgApply(0,'BTLF')
    elseif udg_RsgTick==6 then
        call IssuePointOrder(udg_RsgUnit[2],"move",-1280.0,-400.0)
        call RsgApply(2,'BTLF')
        call RsgApply(0,'BTLF')
    elseif udg_RsgTick==10 then
        call IssuePointOrder(udg_RsgUnit[2],"move",-1280.0,-400.0)
        call RsgApply(2,'BTLF')
    elseif udg_RsgTick==16 then
        call IssuePointOrder(udg_RsgUnit[1],"move",-1408.0,-400.0)
        call RsgApply(1,'BTLF')
    elseif udg_RsgTick==20 then
        call IssuePointOrder(udg_RsgUnit[1],"move",-1408.0,-400.0)
        call RsgApply(1,'BTLF')
    elseif udg_RsgTick==11 then
        call IssuePointOrder(udg_RsgUnit[0],"move",-1536.0,-400.0)
        call RsgApply(0,'BHwe')
    endif
    loop
        exitwhen i==4
        call RsgSample(i,"sample")
        set i=i+1
    endloop
    if udg_RsgTick==40 then
        call RsgMark("label=complete")
        call TimerStart(udg_RsgTimer,100000.0,false,null)
        call PreloadGenEnd("rs-enrollment158.txt")
    endif
endfunction
function PathProbeInit takes nothing returns nothing
    local integer i=0
    call PreloadGenStart()
    call RsgMark("label=start")
    loop
        exitwhen i==4
        set udg_RsgUnit[i]=CreateUnit(Player(0),'hRA0',-1536.0+I2R(i)*128.0,-976.0,90.0)
        set i=i+1
    endloop
    set udg_RsgTimer=CreateTimer()
    call TimerStart(udg_RsgTimer,0.1,true,function RsgTick)
endfunction
