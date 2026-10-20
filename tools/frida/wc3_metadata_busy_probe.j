globals
 unit udg_PathProbeUnit=null
 timer udg_PathProbeTimer=null
 integer udg_PathProbeTick=0
 integer udg_PathMetaRow=0
 hashtable udg_PathMetaTable=null
endglobals
function PathMetaRecord takes string label, boolean accepted returns nothing
 local real speed=GetUnitMoveSpeed(udg_PathProbeUnit)
 call SaveInteger(udg_PathMetaTable,udg_PathMetaRow,0,udg_PathProbeTick)
 call SaveInteger(udg_PathMetaTable,udg_PathMetaRow,1,GetUnitCurrentOrder(udg_PathProbeUnit))
 if accepted then
  call SaveInteger(udg_PathMetaTable,udg_PathMetaRow,2,1)
 else
  call SaveInteger(udg_PathMetaTable,udg_PathMetaRow,2,0)
 endif
 call SaveReal(udg_PathMetaTable,udg_PathMetaRow,3,speed)
 call SaveReal(udg_PathMetaTable,udg_PathMetaRow,4,GetUnitX(udg_PathProbeUnit))
 call SaveReal(udg_PathMetaTable,udg_PathMetaRow,5,GetUnitY(udg_PathProbeUnit))
 call Preload("PATHMETA row="+I2S(udg_PathMetaRow)+" label="+label+" accepted="+I2S(LoadInteger(udg_PathMetaTable,udg_PathMetaRow,2))+" tick="+I2S(udg_PathProbeTick)+" order="+I2S(GetUnitCurrentOrder(udg_PathProbeUnit))+" speed="+R2S(speed))
 set udg_PathMetaRow=udg_PathMetaRow+1
endfunction
function PathMetaOrder takes string order returns nothing
 local boolean accepted=false
 call Preload("PATHMETA begin="+order)
 set accepted=IssueImmediateOrder(udg_PathProbeUnit,order)
 call PathMetaRecord(order,accepted)
endfunction
function PathProbeTick takes nothing returns nothing
 set udg_PathProbeTick=udg_PathProbeTick+1
 if udg_PathProbeTick==1 then
  call PathMetaRecord("move",IssuePointOrder(udg_PathProbeUnit,"move",1280.0,512.0))
 elseif udg_PathProbeTick==5 or udg_PathProbeTick==6 or udg_PathProbeTick==25 then
  if udg_PathProbeTick==6 then
   call PathMetaRecord("move",IssuePointOrder(udg_PathProbeUnit,"move",1280.0,512.0))
  endif
  call PathMetaOrder("defend")
 elseif udg_PathProbeTick==10 or udg_PathProbeTick==11 or udg_PathProbeTick==27 then
  if udg_PathProbeTick==11 then
   call PathMetaRecord("move",IssuePointOrder(udg_PathProbeUnit,"move",1280.0,512.0))
  endif
  call PathMetaOrder("undefend")
 elseif udg_PathProbeTick==15 then
  call PathMetaOrder("missingorder")
 elseif udg_PathProbeTick==20 then
  call SetPlayerAbilityAvailable(Player(0),'Adef',false)
  call PathMetaOrder("defend")
 elseif udg_PathProbeTick==24 then
  call SetPlayerAbilityAvailable(Player(0),'Adef',true)
 elseif udg_PathProbeTick==60 then
  call PathMetaOrder("stop")
 elseif udg_PathProbeTick==62 then
  call PathMetaOrder("defend")
 elseif udg_PathProbeTick==64 then
  call PathMetaOrder("undefend")
 endif
 call PathMetaRecord("sample",true)
 if udg_PathProbeTick==70 then
  call Preload("PATHMETA complete")
  call Preload("PATHPOSE done=metadata_busy")
  call PauseTimer(udg_PathProbeTimer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call Preload("PATHMETA case=metadata_busy")
 call Preload("PATHPOSE case=metadata_busy")
 set udg_PathMetaTable=InitHashtable()
 set udg_PathProbeUnit=CreateUnit(Player(0),'hfoo',272.0,512.0,0.0)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetCameraPosition(1008.0,1040.0)
 call Preload("PATHTRACE tick=0 label=start_metadata_busy x=272.000 y=512.000 order=0")
 call PathMetaOrder("defend")
 call SetPlayerTechResearched(Player(0),'Rhde',1)
 call PathMetaOrder("undefend")
 set udg_PathProbeTimer=CreateTimer()
 call TimerStart(udg_PathProbeTimer,0.1,true,function PathProbeTick)
endfunction
