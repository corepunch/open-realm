globals
 unit udg_T216Mover=null
 unit udg_T216Target=null
 timer udg_T216Timer=null
 integer udg_T216Tick=0
 integer udg_T216Scene=0
endglobals
function T216Mark takes string label returns nothing
 call Preload("T216 tick="+I2S(udg_T216Tick)+" scene="+I2S(udg_T216Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_T216Mover))+" x="+R2S(GetUnitX(udg_T216Mover))+" y="+R2S(GetUnitY(udg_T216Mover)))
endfunction
function T216Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_T216Tick,60)
 local boolean accepted=false
 local string order="smart"
 if udg_T216Scene>=3 then
  set order="move"
 endif
 if t==0 then
  set udg_T216Mover=CreateUnit(GetLocalPlayer(),'hF00',320.0,1024.0,0.0)
  set udg_T216Target=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'hF00',1760.0,1024.0,0.0)
  if ModuloInteger(udg_T216Scene,3)==2 then
   call SetUnitX(udg_T216Mover,1696.0)
  endif
  call SetUnitAcquireRange(udg_T216Mover,0.0)
  call SetUnitAcquireRange(udg_T216Target,0.0)
  call T216Mark("begin")
 elseif t==5 then
  set accepted=IssueTargetOrder(udg_T216Mover,order,udg_T216Target)
  if not accepted then
   call T216Mark("rejected")
  endif
  call T216Mark("ordered")
  if ModuloInteger(udg_T216Scene,3)==0 then
   call UnitAddAbility(udg_T216Target,'Apiv')
   call T216Mark("immediate_loss")
  endif
 elseif t==15 and ModuloInteger(udg_T216Scene,3)!=0 then
  call SetUnitY(udg_T216Target,1280.0)
  call T216Mark("before_loss")
  call UnitAddAbility(udg_T216Target,'Apiv')
  call T216Mark("active_loss")
 elseif t==20 then
  call UnitRemoveAbility(udg_T216Target,'Apiv')
  call T216Mark("reveal")
 elseif t==54 then
  call T216Mark("settled")
 elseif t==55 then
  call IssueImmediateOrder(udg_T216Mover,"stop")
  call RemoveUnit(udg_T216Mover)
  call RemoveUnit(udg_T216Target)
 elseif t==59 then
  if udg_T216Scene==5 then
   call T216Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_T216Timer)
  endif
  set udg_T216Scene=udg_T216Scene+1
 endif
 set udg_T216Tick=udg_T216Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1027)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(GetLocalPlayer(),Player(PLAYER_NEUTRAL_PASSIVE),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE),GetLocalPlayer(),ALLIANCE_PASSIVE,true)
 call Preload("T216 tick=0 scene=0 label=start order=0")
 call PreloadGenEnd("@START@")
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_T216Timer=CreateTimer()
 call TimerStart(udg_T216Timer,0.1,true,function T216Tick)
endfunction
