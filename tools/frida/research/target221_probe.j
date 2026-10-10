globals
 unit udg_T221Mover=null
 unit udg_T221Target=null
 timer udg_T221Timer=null
 integer udg_T221Tick=0
 integer udg_T221Scene=0
endglobals
function T221Mark takes string label returns nothing
 call Preload("T221 tick="+I2S(udg_T221Tick)+" scene="+I2S(udg_T221Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_T221Mover))+" owner="+I2S(GetPlayerId(GetOwningPlayer(udg_T221Target)))+" x="+R2S(GetUnitX(udg_T221Mover))+" y="+R2S(GetUnitY(udg_T221Mover)))
endfunction
function T221Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_T221Tick,70)
 local integer p=0
 local boolean accepted=false
 if t==0 then
  set udg_T221Mover=CreateUnit(GetLocalPlayer(),'hF00',1344,1024,0)
  set udg_T221Target=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'hF00',1568,1024,0)
  if udg_T221Scene==4 then
   call SetUnitX(udg_T221Mover,320)
  endif
  call SetUnitAcquireRange(udg_T221Mover,0)
  call SetUnitAcquireRange(udg_T221Target,0)
  call T221Mark("begin")
 elseif t==1 and udg_T221Scene==0 then
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif t==5 then
  if udg_T221Scene==3 then
   set accepted=IssueTargetOrder(udg_T221Mover,"move",udg_T221Target)
  else
   set accepted=IssueTargetOrder(udg_T221Mover,"smart",udg_T221Target)
  endif
  if accepted then
   call T221Mark("accepted")
  else
   call T221Mark("rejected")
  endif
 elseif t==18 and udg_T221Scene==5 then
  call PauseUnit(udg_T221Target,true)
 elseif t==20 then
  call T221Mark("before_transfer")
  if udg_T221Scene==2 then
   call SetUnitOwner(udg_T221Target,Player(PLAYER_NEUTRAL_PASSIVE),false)
  elseif udg_T221Scene==6 then
   call SetUnitOwner(udg_T221Mover,Player(2),false)
  elseif udg_T221Scene==1 then
   call SetUnitOwner(udg_T221Target,Player(1),false)
  else
   call SetUnitOwner(udg_T221Target,Player(2),false)
  endif
  call T221Mark("after_transfer")
 elseif t==40 then
  call SetUnitOwner(udg_T221Target,Player(PLAYER_NEUTRAL_PASSIVE),false)
  call PauseUnit(udg_T221Target,false)
  call T221Mark("restored")
 elseif t==60 then
  call T221Mark("settled")
 elseif t==65 then
  call RemoveUnit(udg_T221Mover)
  call RemoveUnit(udg_T221Target)
 elseif t==69 then
  if udg_T221Scene==6 then
   call T221Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_T221Timer)
  endif
  set udg_T221Scene=udg_T221Scene+1
 endif
 if t<65 then
  call T221Mark("sample")
 endif
 set udg_T221Tick=udg_T221Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local integer p=0
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1028)
 call FogEnable(false)
 call FogMaskEnable(false)
 loop
  exitwhen p>15
  call SetPlayerAlliance(GetLocalPlayer(),Player(p),ALLIANCE_PASSIVE,p!=1)
  call SetPlayerAlliance(Player(p),GetLocalPlayer(),ALLIANCE_PASSIVE,p!=1)
  set p=p+1
 endloop
 set udg_T221Timer=CreateTimer()
 call TimerStart(udg_T221Timer,0.1,true,function T221Tick)
 call Preload("T221 tick=0 scene=0 label=start order=0")
endfunction
