globals
 unit udg_PathProbeUnit=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_PathProbeCase=0
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" case="+I2S(udg_PathProbeCase)+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeBirth takes nothing returns nothing
 local integer cls=ModuloInteger(udg_PathProbeCase,4)
 if udg_PathProbeUnit!=null then
  call RemoveUnit(udg_PathProbeUnit)
 endif
 call SetTerrainPathable(272.0,304.0,PATHING_TYPE_WALKABILITY,true)
 if cls==0 then
  set udg_PathProbeUnit=CreateUnit(Player(0),'hF91',272.0,304.0,90.0)
 elseif cls==1 then
  set udg_PathProbeUnit=CreateUnit(Player(0),'hF92',272.0,304.0,90.0)
 elseif cls==2 then
  set udg_PathProbeUnit=CreateUnit(Player(0),'hF93',272.0,304.0,90.0)
 else
  set udg_PathProbeUnit=CreateUnit(Player(0),'hF94',272.0,304.0,90.0)
 endif
 call SetUnitMoveSpeed(udg_PathProbeUnit,150.0)
 call PathProbeRecord("birth")
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if ModuloInteger(udg_PathProbeTick,80)==10 then
  if udg_PathProbeCase<4 then
   call SetUnitX(udg_PathProbeUnit,257.0)
   call SetUnitY(udg_PathProbeUnit,289.0)
   call IssuePointOrder(udg_PathProbeUnit,"move",287.0,319.0)
   call PathProbeRecord("same_cell_order")
  elseif udg_PathProbeCase<8 then
   call SetUnitX(udg_PathProbeUnit,272.0)
   call SetUnitY(udg_PathProbeUnit,304.0)
   call SetTerrainPathable(272.0,304.0,PATHING_TYPE_WALKABILITY,false)
   call PathProbeRecord("blocked_source")
   call IssuePointOrder(udg_PathProbeUnit,"move",624.0,632.0)
   call PathProbeRecord("blocked_source_order")
  else
   call IssuePointOrder(udg_PathProbeUnit,"move",624.0,632.0)
   call PathProbeRecord("moving_before_block")
  endif
 endif
 if udg_PathProbeCase>=8 and ModuloInteger(udg_PathProbeTick,80)==12 then
  call SetTerrainPathable(GetUnitX(udg_PathProbeUnit),GetUnitY(udg_PathProbeUnit),PATHING_TYPE_WALKABILITY,false)
  call PathProbeRecord("block_current_source")
 endif
 call PathProbeRecord("sample")
 if ModuloInteger(udg_PathProbeTick,80)==0 then
  set udg_PathProbeCase=udg_PathProbeCase+1
  if udg_PathProbeCase==12 then
   call PathProbeRecord("complete")
   call PauseTimer(udg_PathProbeTimer)
  else
   call PathProbeBirth()
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeBirth()
 call PathProbeRecord("start_movement_bypasses")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
