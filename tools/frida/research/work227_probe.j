globals
 unit udg_W227Worker=null
 unit udg_W227Building=null
 group udg_W227Group=null
 timer udg_W227Timer=null
 integer udg_W227Tick=0
 integer udg_W227Scene=0
 integer udg_W227BuildingType=0
endglobals
function W227Find takes nothing returns nothing
 if GetUnitTypeId(GetEnumUnit())==udg_W227BuildingType then
  set udg_W227Building=GetEnumUnit()
 endif
endfunction
function W227ConstructStart takes nothing returns nothing
 if GetUnitTypeId(GetConstructingStructure())==udg_W227BuildingType then
  set udg_W227Building=GetConstructingStructure()
 endif
endfunction
function W227Mark takes string label returns nothing
 local integer paused=0
 local integer hidden=0
 if IsUnitPaused(udg_W227Worker) then
  set paused=1
 endif
 if IsUnitHidden(udg_W227Worker) then
  set hidden=1
 endif
 call Preload("W227 tick="+I2S(udg_W227Tick)+" scene="+I2S(udg_W227Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_W227Worker))+" owner="+I2S(GetPlayerId(GetOwningPlayer(udg_W227Worker)))+" paused="+I2S(paused)+" hidden="+I2S(hidden)+" building="+I2S(GetUnitTypeId(udg_W227Building))+" x="+R2S(GetUnitX(udg_W227Worker))+" y="+R2S(GetUnitY(udg_W227Worker)))
endfunction
function W227Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_W227Tick,70)
 local boolean accepted=false
 local integer unitType='oW27'
 if udg_W227Scene==1 then
  set unitType='oN27'
 elseif udg_W227Scene==2 or udg_W227Scene==5 or udg_W227Scene==7 then
  set unitType='nW27'
 elseif udg_W227Scene==3 then
  set unitType='nN27'
 endif
 if t==0 then
  set udg_W227Building=null
  set udg_W227BuildingType='otrb'
  if udg_W227Scene==2 or udg_W227Scene==3 or udg_W227Scene==7 then
   set udg_W227BuildingType='nnfm'
  elseif udg_W227Scene==4 then
   set udg_W227BuildingType='oB27'
  elseif udg_W227Scene==5 then
   set udg_W227BuildingType='nB27'
  endif
  set udg_W227Worker=CreateUnit(Player(0),unitType,640,1024,0)
  call SetUnitAcquireRange(udg_W227Worker,0)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD,100000)
  call SetPlayerState(Player(0),PLAYER_STATE_RESOURCE_LUMBER,100000)
  call W227Mark("created")
 elseif t==1 and udg_W227Scene==0 then
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif t==5 then
  set accepted=IssueBuildOrderById(udg_W227Worker,udg_W227BuildingType,768,1024)
  if not accepted then
   call W227Mark("rejected")
  endif
  call W227Mark("ordered")
 elseif t==15 then
  call PauseUnit(udg_W227Worker,true)
  call W227Mark("paused")
 elseif t==20 then
  call PauseUnit(udg_W227Worker,false)
  call W227Mark("resumed")
 elseif t==25 and (udg_W227Scene<4 or udg_W227Scene>=6) then
  call PauseUnit(udg_W227Worker,true)
  call W227Mark("paused_again")
 elseif t==30 and (udg_W227Scene<4 or udg_W227Scene>=6) then
  call GroupEnumUnitsInRange(udg_W227Group,768,1024,256,null)
  call ForGroup(udg_W227Group,function W227Find)
  call GroupClear(udg_W227Group)
  if udg_W227Scene<4 then
   call KillUnit(udg_W227Building)
   call W227Mark("target_killed")
  else
   call RemoveUnit(udg_W227Building)
   call W227Mark("target_removed")
  endif
 elseif t==35 then
  call SetUnitOwner(udg_W227Worker,Player(1),true)
  call W227Mark("owner_changed")
 elseif t==40 then
  call PauseUnit(udg_W227Worker,false)
  call W227Mark("unpaused")
 elseif t==45 then
  set accepted=IssuePointOrder(udg_W227Worker,"move",1280,1024)
  if not accepted then
   call W227Mark("move_rejected")
  endif
  call W227Mark("move")
 elseif t==60 then
  call RemoveUnit(udg_W227Worker)
  if udg_W227Scene>=4 and udg_W227Scene<6 then
   call GroupEnumUnitsInRange(udg_W227Group,768,1024,256,null)
   call ForGroup(udg_W227Group,function W227Find)
   call GroupClear(udg_W227Group)
   call RemoveUnit(udg_W227Building)
  endif
 elseif t==69 then
  if udg_W227Scene==7 then
   call W227Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_W227Timer)
  endif
  set udg_W227Scene=udg_W227Scene+1
 endif
 if t<60 then
  call W227Mark("sample")
 endif
 set udg_W227Tick=udg_W227Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger construction=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1027)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 set udg_W227Group=CreateGroup()
 call TriggerRegisterPlayerUnitEvent(construction,Player(0),EVENT_PLAYER_UNIT_CONSTRUCT_START,null)
 call TriggerAddAction(construction,function W227ConstructStart)
 set udg_W227Timer=CreateTimer()
 call TimerStart(udg_W227Timer,0.1,true,function W227Tick)
 call Preload("W227 tick=0 scene=0 label=start")
endfunction
