globals
 unit udg_W226Worker=null
 unit udg_W226Building=null
 group udg_W226Group=null
 timer udg_W226Timer=null
 integer udg_W226Tick=0
 integer udg_W226Scene=0
endglobals
function W226Find takes nothing returns nothing
 if GetUnitTypeId(GetEnumUnit())=='hhou' then
  set udg_W226Building=GetEnumUnit()
 endif
endfunction
function W226Mark takes string label returns nothing
 local integer paused=0
 if IsUnitPaused(udg_W226Worker) then
  set paused=1
 endif
 call Preload("W226 tick="+I2S(udg_W226Tick)+" scene="+I2S(udg_W226Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_W226Worker))+" owner="+I2S(GetPlayerId(GetOwningPlayer(udg_W226Worker)))+" paused="+I2S(paused)+" x="+R2S(GetUnitX(udg_W226Worker))+" y="+R2S(GetUnitY(udg_W226Worker)))
endfunction
function W226Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_W226Tick,70)
 local boolean accepted=false
 local integer unitType='hW26'
 if udg_W226Scene==2 then
  set unitType='hN26'
 endif
 if t==0 then
  set udg_W226Worker=CreateUnit(Player(0),unitType,640,1024,0)
  call SetUnitAcquireRange(udg_W226Worker,0)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
  if udg_W226Scene==0 then
   set udg_W226Building=CreateUnit(Player(0),'hhou',768,1024,0)
   call SetUnitState(udg_W226Building,UNIT_STATE_LIFE,100)
  endif
  call W226Mark("created")
 elseif t==1 and udg_W226Scene==0 then
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif t==5 then
  if udg_W226Scene==0 then
   set accepted=IssueTargetOrder(udg_W226Worker,"repair",udg_W226Building)
  else
   set accepted=IssueBuildOrderById(udg_W226Worker,'hhou',768,1024)
  endif
  if not accepted then
   call W226Mark("rejected")
  endif
  call W226Mark("ordered")
 elseif t==15 then
  call PauseUnit(udg_W226Worker,true)
  call W226Mark("paused")
 elseif t==20 then
  call PauseUnit(udg_W226Worker,false)
  call W226Mark("resumed")
 elseif t==25 then
  call IssueImmediateOrder(udg_W226Worker,"stop")
  call W226Mark("stopped")
 elseif t==30 then
  if udg_W226Scene!=0 then
   call GroupEnumUnitsInRange(udg_W226Group,768,1024,256,null)
   call ForGroup(udg_W226Group,function W226Find)
   call GroupClear(udg_W226Group)
  endif
  set accepted=IssueTargetOrder(udg_W226Worker,"repair",udg_W226Building)
  if not accepted then
   call W226Mark("restart_rejected")
  endif
  call W226Mark("restarted")
 elseif t==40 then
  call RemoveUnit(udg_W226Building)
  set udg_W226Building=null
  call W226Mark("target_removed")
 elseif t==50 then
  call IssuePointOrder(udg_W226Worker,"move",1280,1024)
  call W226Mark("move")
 elseif t==65 then
  call RemoveUnit(udg_W226Worker)
 elseif t==69 then
  if udg_W226Scene==2 then
   call W226Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_W226Timer)
  endif
  set udg_W226Scene=udg_W226Scene+1
 endif
 if t<65 then
  call W226Mark("sample")
 endif
 set udg_W226Tick=udg_W226Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1027)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 set udg_W226Group=CreateGroup()
 set udg_W226Timer=CreateTimer()
 call TimerStart(udg_W226Timer,0.1,true,function W226Tick)
 call Preload("W226 tick=0 scene=0 label=start")
endfunction
