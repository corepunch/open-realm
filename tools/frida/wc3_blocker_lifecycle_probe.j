// Public depletion/destruction/removal producers; the observer only reads grids.
globals
    unit udg_PathProbeUnit = null
    unit udg_PathProbeWorker = null
    unit udg_PathProbeMine = null
    destructable udg_PathProbeTree = null
    destructable udg_PathProbePeer = null
    destructable udg_PathProbeGate = null
    timer udg_PathProbeTimer = null
    integer udg_PathProbeTick = 0
    boolean udg_PathProbeTreeDone = false
    boolean udg_PathProbeMineDone = false
endglobals

function PathProbeRecord takes string label returns nothing
    call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction

function PathProbeLife takes string label returns nothing
    call Preload("PATHLIFE label="+label+" tree="+R2S(GetDestructableLife(udg_PathProbeTree))+" peer="+R2S(GetDestructableLife(udg_PathProbePeer))+" gate="+R2S(GetDestructableLife(udg_PathProbeGate))+" mine="+R2S(GetWidgetLife(udg_PathProbeMine))+" gold="+I2S(GetResourceAmount(udg_PathProbeMine))+" worker="+I2S(GetUnitCurrentOrder(udg_PathProbeWorker)))
    call PathProbeRecord(label)
endfunction

function PathProbeRequest takes string label returns nothing
    if IssuePointOrder(udg_PathProbeUnit,"move",-1936.0,-416.0) then
        call PathProbeRecord(label+"_accepted")
    else
        call PathProbeRecord(label+"_rejected")
    endif
endfunction

function PathProbeTick takes nothing returns nothing
    set udg_PathProbeTick=udg_PathProbeTick+1
    if udg_PathProbeTick==5 then
        set udg_PathProbeTree=CreateDestructable('LTlt',-1936.0,-560.0,0.0,1.0,0)
        set udg_PathProbePeer=CreateDestructable('LTlt',-1936.0,-560.0,0.0,1.0,0)
        call SetDestructableLife(udg_PathProbeTree,1.0)
        call PathProbeLife("trees_created")
        if IssueTargetOrder(udg_PathProbeWorker,"harvest",udg_PathProbeTree) then
            call PathProbeRecord("lumber_accepted")
        else
            call PathProbeRecord("lumber_rejected")
        endif
    endif
    if udg_PathProbeTree!=null and not udg_PathProbeTreeDone and GetDestructableLife(udg_PathProbeTree)<=0.0 then
        set udg_PathProbeTreeDone=true
        call IssueImmediateOrder(udg_PathProbeWorker,"stop")
        call PathProbeLife("tree_depleted_overlap")
    endif
    if udg_PathProbeTick==100 then
        if not udg_PathProbeTreeDone then
            call PathProbeLife("tree_depletion_failed")
        endif
        call PathProbeLife("before_tree_remove")
        call RemoveDestructable(udg_PathProbePeer)
        set udg_PathProbePeer=null
        call RemoveDestructable(udg_PathProbeTree)
        set udg_PathProbeTree=null
        call PathProbeLife("trees_removed")
        call PathProbeRequest("after_trees")
    endif
    if udg_PathProbeTick==102 then
        call IssueImmediateOrder(udg_PathProbeUnit,"stop")
    endif
    if udg_PathProbeTick==105 then
        call RemoveUnit(udg_PathProbeWorker)
        set udg_PathProbeWorker=CreateUnit(Player(0),'hpea',-1936.0,-912.0,90.0)
        set udg_PathProbeMine=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'ngol',-1936.0,-560.0,0.0)
        call SetResourceAmount(udg_PathProbeMine,6)
        call PathProbeLife("mine_created")
        if IssueTargetOrder(udg_PathProbeWorker,"harvest",udg_PathProbeMine) then
            call PathProbeRecord("gold_accepted")
        else
            call PathProbeRecord("gold_rejected")
        endif
    endif
    if udg_PathProbeMine!=null and not udg_PathProbeMineDone and GetResourceAmount(udg_PathProbeMine)==0 and GetWidgetLife(udg_PathProbeMine)<=0.0 then
        set udg_PathProbeMineDone=true
        call IssueImmediateOrder(udg_PathProbeWorker,"stop")
        call PathProbeLife("mine_depleted")
        call PathProbeRequest("after_mine_depletion")
    endif
    if udg_PathProbeTick==205 then
        if not udg_PathProbeMineDone then
            call PathProbeLife("mine_depletion_failed")
        endif
        call IssueImmediateOrder(udg_PathProbeUnit,"stop")
        call PathProbeLife("before_mine_remove")
        call RemoveUnit(udg_PathProbeMine)
        set udg_PathProbeMine=null
        call PathProbeLife("mine_removed")
        call PathProbeRequest("after_mine_removal")
    endif
    if udg_PathProbeTick==210 then
        call IssueImmediateOrder(udg_PathProbeUnit,"stop")
        set udg_PathProbeGate=CreateDestructable('LTg1',-1936.0,-560.0,0.0,1.0,0)
        call PathProbeLife("gate_created")
    endif
    if udg_PathProbeTick==215 then
        call KillDestructable(udg_PathProbeGate)
        call PathProbeLife("gate_killed")
    endif
    if udg_PathProbeTick==220 then
        call DestructableRestoreLife(udg_PathProbeGate,100.0,false)
        call PathProbeLife("gate_restored")
    endif
    if udg_PathProbeTick==225 then
        call RemoveDestructable(udg_PathProbeGate)
        set udg_PathProbeGate=null
        call PathProbeLife("gate_removed")
        call PathProbeRequest("after_gate_removal")
    endif
    if udg_PathProbeTick==250 then
        call PathProbeRecord("complete")
        call PauseTimer(udg_PathProbeTimer)
    endif
endfunction

function PathProbeInit takes nothing returns nothing
    set udg_PathProbeUnit=CreateUnit(Player(0),'hfoo',-2464.0,-560.0,90.0)
    set udg_PathProbeWorker=CreateUnit(Player(0),'hpea',-1936.0,-720.0,90.0)
    call FogEnable(false)
    call FogMaskEnable(false)
    call PathProbeLife("start_blocker_lifecycle")
    set udg_PathProbeTimer=CreateTimer()
    call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
