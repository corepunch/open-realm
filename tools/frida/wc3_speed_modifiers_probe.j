globals
 unit udg_PathProbeUnit=null
 unit udg_PathCaster=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 boolean udg_CasterHeld=false
endglobals
function PathProbeRecord takes string label returns nothing
 call Preload("PATHBUFF slow="+I2S(GetUnitAbilityLevel(udg_PathProbeUnit,'Bslo'))+" haste="+I2S(GetUnitAbilityLevel(udg_PathProbeUnit,'Bblo'))+" speed="+R2S(GetUnitMoveSpeed(udg_PathProbeUnit)))
 call Preload("PATHTRACE tick="+I2S(udg_PathProbeTick)+" label="+label+" x="+R2S(GetUnitX(udg_PathProbeUnit))+" y="+R2S(GetUnitY(udg_PathProbeUnit))+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit)))
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==10 then
  call IssuePointOrder(udg_PathProbeUnit,"move",1712.0,1712.0)
  call PathProbeRecord("point_move")
 elseif udg_PathProbeTick==35 then
  set udg_PathCaster=CreateUnit(Player(1),'hsor',GetUnitX(udg_PathProbeUnit)-320.0,GetUnitY(udg_PathProbeUnit),0.0)
  call IssueImmediateOrder(udg_PathCaster,"slowoff")
  call IssueImmediateOrder(udg_PathCaster,"holdposition")
  call PathProbeRecord("caster_created")
 elseif udg_PathProbeTick==40 then
  if IssueTargetOrder(udg_PathCaster,"slow",udg_PathProbeUnit) then
   call Preload("PATHCAST slow=1")
  else
   call Preload("PATHCAST slow=0")
  endif
  call PathProbeRecord("slow_requested")
 elseif udg_PathProbeTick==90 then
  call UnitRemoveBuffsEx(udg_PathProbeUnit,false,true,true,false,false,false,true)
  call PathProbeRecord("slow_removed")
 elseif udg_PathProbeTick==110 then
  call IssuePointOrder(udg_PathProbeUnit,"move",272.0,304.0)
  call PathProbeRecord("reverse_move")
 elseif udg_PathProbeTick==100 then
  call PauseUnit(udg_PathCaster,false)
  call SetUnitOwner(udg_PathCaster,Player(0),false)
  call SetUnitPosition(udg_PathCaster,GetUnitX(udg_PathProbeUnit)-320.0,GetUnitY(udg_PathProbeUnit))
  call UnitAddAbility(udg_PathCaster,'ACbl')
  if IssueTargetOrder(udg_PathCaster,"bloodlust",udg_PathProbeUnit) then
   call Preload("PATHCAST haste=1")
  else
   call Preload("PATHCAST haste=0")
  endif
  set udg_CasterHeld=false
  call PathProbeRecord("haste_requested")
 elseif udg_PathProbeTick==140 then
  call UnitRemoveBuffs(udg_PathProbeUnit,true,false)
  call PathProbeRecord("haste_removed")
 endif
 if not udg_CasterHeld and udg_PathCaster!=null and (GetUnitAbilityLevel(udg_PathProbeUnit,'Bslo')>0 or GetUnitAbilityLevel(udg_PathProbeUnit,'Bblo')>0) then
  call IssueImmediateOrder(udg_PathCaster,"holdposition")
  call PauseUnit(udg_PathCaster,true)
  set udg_CasterHeld=true
  call PathProbeRecord("modifier_applied")
  call UnitRemoveBuffs(udg_PathProbeUnit,false,false)
  call PathProbeRecord("remove_neither")
  if GetUnitAbilityLevel(udg_PathProbeUnit,'Bslo')>0 then
   call UnitRemoveBuffs(udg_PathProbeUnit,true,false)
  else
   call UnitRemoveBuffs(udg_PathProbeUnit,false,true)
  endif
  call PathProbeRecord("remove_other_polarity")
  call UnitRemoveBuffsEx(udg_PathProbeUnit,true,true,false,true,false,false,true)
  call PathProbeRecord("remove_physical")
 endif
 call PathProbeRecord("sample")
 if udg_PathProbeTick==300 then
  call PathProbeRecord("complete")
  call Preload("PATHPOSE done=speed_modifiers")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHPOSE case=speed_modifiers")
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 set udg_PathProbeUnit=CreateUnit(Player(0),'hV80',272.0,304.0,90.0)
 call SetUnitMoveSpeed(udg_PathProbeUnit,270.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 call PathProbeRecord("start_speed_modifiers")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
