// Public final-binding removal/replacement and subsequent existing-captain refill.
// Included in the ordinary captain_home producer; no observer state writes.
function PathCaptainLifetimeRecord takes integer operation returns nothing
    local integer i=0
    local integer row=0
    loop
        exitwhen i==13
        set row=udg_PathProbeTick*13+i
        call SaveInteger(udg_CaptainLifetimeTable,row,0,udg_PathProbeTick)
        call SaveInteger(udg_CaptainLifetimeTable,row,1,GetUnitCurrentOrder(udg_PathProbeCrowd[i]))
        call SaveInteger(udg_CaptainLifetimeTable,row,2,operation)
        call SaveReal(udg_CaptainLifetimeTable,row,3,GetUnitMoveSpeed(udg_PathProbeCrowd[i]))
        call SaveReal(udg_CaptainLifetimeTable,row,4,GetUnitX(udg_PathProbeCrowd[i]))
        call SaveReal(udg_CaptainLifetimeTable,row,5,GetUnitY(udg_PathProbeCrowd[i]))
        set i=i+1
    endloop
endfunction

function PathCaptainLifetimeTick takes integer operation returns nothing
    local integer i=0
    if udg_PathProbeTick==92 then
        call Preload("PATHCAPTAIN lifetime before cancellation")
        loop
            exitwhen i==13
            if operation==0 then
                call RemoveUnit(udg_PathProbeCrowd[i])
            elseif operation==1 then
                call IssuePointOrder(udg_PathProbeCrowd[i],"move",GetUnitX(udg_PathProbeCrowd[i])+128.0,GetUnitY(udg_PathProbeCrowd[i]))
            else
                call IssueImmediateOrder(udg_PathProbeCrowd[i],"stop")
            endif
            set i=i+1
        endloop
        call Preload("PATHCAPTAIN lifetime after cancellation")
    endif
    if operation==0 and udg_PathProbeTick==99 then
        call Preload("PATHCAPTAIN lifetime before fresh units")
        loop
            exitwhen i==13
            if i==0 then
                set udg_PathProbeCrowd[i]=CreateUnit(Player(0),'hCLG',-1936.0,-976.0,90.0)
            else
                set udg_PathProbeCrowd[i]=CreateUnit(Player(0),'hfoo',-1936.0+I2R(ModuloInteger(i,4))*80.0,-976.0-I2R(i/4)*80.0,90.0)
            endif
            call SetUnitMoveSpeed(udg_PathProbeCrowd[i],100.0)
            set i=i+1
        endloop
        set udg_PathProbeUnit=udg_PathProbeCrowd[0]
        call Preload("PATHCAPTAIN lifetime after fresh units")
    endif
    if udg_PathProbeTick==92 or udg_PathProbeTick==93 or udg_PathProbeTick==99 or udg_PathProbeTick==100 or udg_PathProbeTick==101 or udg_PathProbeTick==200 or udg_PathProbeTick==400 then
        call PathCaptainLifetimeRecord(operation)
    endif
    if udg_PathProbeTick==400 then
        call Preload("PATHMETA complete")
    endif
endfunction
