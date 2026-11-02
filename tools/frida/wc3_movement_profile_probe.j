globals
    unit array udg_ProfileUnits
    timer udg_ProfileTimer=null
    integer udg_ProfileTick=0
    unit udg_PathProbeUnit=null
endglobals

function PathProbeRecord takes string label returns nothing
    call Preload("PATHTRACE tick="+I2S(udg_ProfileTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction

function ProfileAccepted takes boolean accepted returns string
    if accepted then
        return "1"
    endif
    return "0"
endfunction

function ProfileSnapshot takes string label returns nothing
    local integer i=0
    local unit u
    loop
        exitwhen i==15
        set u=udg_ProfileUnits[i]
        call Preload("PATHSTOCK tick="+I2S(udg_ProfileTick)+" label="+label+" i="+I2S(i)+" type="+I2S(GetUnitTypeId(u))+" x="+R2S(GetUnitX(u))+" y="+R2S(GetUnitY(u))+" speed="+R2S(GetUnitMoveSpeed(u))+" order="+I2S(GetUnitCurrentOrder(u)))
        set i=i+1
    endloop
    set u=null
endfunction

function ProfileTick takes nothing returns nothing
    local integer i=0
    set udg_ProfileTick=udg_ProfileTick+1
    if udg_ProfileTick==10 then
        loop
            exitwhen i==15
            call Preload("PATHSTOCK tick=10 label=order i="+I2S(i)+" accepted="+ProfileAccepted(IssuePointOrder(udg_ProfileUnits[i],"move",GetUnitX(udg_ProfileUnits[i])+256.0,GetUnitY(udg_ProfileUnits[i]))))
            set i=i+1
        endloop
    endif
    if udg_ProfileTick==1 or udg_ProfileTick==9 or udg_ProfileTick==11 or udg_ProfileTick==30 or udg_ProfileTick==100 or udg_ProfileTick==150 then
        call ProfileSnapshot("state")
    endif
    call PathProbeRecord("sample")
    if udg_ProfileTick==150 then
        call PathProbeRecord("complete")
        call PreloadGenEnd("pathtrace-movement_profiles131.txt")
        call PauseTimer(udg_ProfileTimer)
    endif
endfunction

function PathProbeInit takes nothing returns nothing
    local integer i=0
    local integer array ids
    call PreloadGenClear()
    call PreloadGenStart()
    set ids[0]='hM00'
    set ids[1]='hM01'
    set ids[2]='hM02'
    set ids[3]='hM03'
    set ids[4]='hM04'
    set ids[5]='hM05'
    set ids[6]='hM06'
    set ids[7]='hM07'
    set ids[8]='hM08'
    set ids[9]='hM09'
    set ids[10]='hM10'
    set ids[11]='hM11'
    set ids[12]='hM12'
    set ids[13]='hM13'
    set ids[14]='hM14'
    loop
        exitwhen i==15
        set udg_ProfileUnits[i]=CreateUnit(Player(0),ids[i],-2432.0+I2R(ModuloInteger(i,8))*160.0,-1344.0+I2R(i/8)*160.0,0.0)
        call SetUnitAcquireRange(udg_ProfileUnits[i],0.0)
        call SetUnitInvulnerable(udg_ProfileUnits[i],true)
        set i=i+1
    endloop
    set udg_PathProbeUnit=udg_ProfileUnits[0]
    call FogEnable(false)
    call FogMaskEnable(false)
    call PathProbeRecord("start_profiles")
    call ProfileSnapshot("created")
    set udg_ProfileTimer=CreateTimer()
    call TimerStart(udg_ProfileTimer,0.1,true,function ProfileTick)
endfunction
