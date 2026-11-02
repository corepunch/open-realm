globals
 unit udg_PathProbeUnit=null
 destructable udg_PathProbeTree=null
 destructable udg_PathProbeGate=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeLife takes string label returns nothing
 call Preload("PATHLIFE label="+label+" tree="+R2S(GetDestructableLife(udg_PathProbeTree))+" treeX="+R2S(GetDestructableX(udg_PathProbeTree))+" treeY="+R2S(GetDestructableY(udg_PathProbeTree))+" gate="+R2S(GetDestructableLife(udg_PathProbeGate))+" gateX="+R2S(GetDestructableX(udg_PathProbeGate))+" gateY="+R2S(GetDestructableY(udg_PathProbeGate)))
 call PathProbeRecord(label)
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==5 then
  set udg_PathProbeTree=CreateDestructable('LTlt',-1936.0,-560.0,0.0,1.0,0)
  call PathProbeLife("tree_first")
 elseif udg_PathProbeTick==10 then
  set udg_PathProbeGate=CreateDestructable('LTg1',-1936.0,-560.0,0.0,1.0,0)
  call PathProbeLife("tree_then_gate")
 elseif udg_PathProbeTick==15 then
  call RemoveDestructable(udg_PathProbeTree)
  set udg_PathProbeTree=null
  call PathProbeLife("tree_first_removed")
 elseif udg_PathProbeTick==20 then
  call RemoveDestructable(udg_PathProbeGate)
  set udg_PathProbeGate=null
  call PathProbeLife("first_pair_removed")
 elseif udg_PathProbeTick==25 then
  set udg_PathProbeGate=CreateDestructable('LTg1',-1936.0,-560.0,0.0,1.0,0)
  call PathProbeLife("gate_first")
 elseif udg_PathProbeTick==30 then
  set udg_PathProbeTree=CreateDestructable('LTlt',-1936.0,-560.0,0.0,1.0,0)
  call PathProbeLife("gate_then_tree")
 elseif udg_PathProbeTick==35 then
  call RemoveDestructable(udg_PathProbeGate)
  set udg_PathProbeGate=null
  call PathProbeLife("gate_first_removed")
 elseif udg_PathProbeTick==40 then
  call RemoveDestructable(udg_PathProbeTree)
  set udg_PathProbeTree=null
  call PathProbeLife("second_pair_removed")
 elseif udg_PathProbeTick==45 then
  call PathProbeRecord("complete")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 set udg_PathProbeUnit=CreateUnit(Player(0),'hfoo',-2464.0,-560.0,90.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PathProbeLife("start_blocker_lifecycle")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
