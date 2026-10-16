globals
 unit udg_T327Unit=null
 unit udg_T327Target=null
 item udg_T327Item=null
 timer udg_T327Timer=null
 integer udg_T327Tick=0
 integer udg_T327Scene=0
endglobals
function T327Mark takes string label returns nothing
 local integer paused=0
 if IsUnitPaused(udg_T327Unit) then
  set paused=1
 endif
 call Preload("T327 tick="+I2S(udg_T327Tick)+" scene="+I2S(udg_T327Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_T327Unit))+" order="+I2S(GetUnitCurrentOrder(udg_T327Unit))+" paused="+I2S(paused)+" x="+R2S(GetUnitX(udg_T327Unit))+" y="+R2S(GetUnitY(udg_T327Unit)))
 call PreloadGenEnd("@OUTPUT@")
endfunction
function T327Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_T327Tick,100)
 local boolean ok=false
 if age==0 then
  set udg_T327Unit=CreateUnit(Player(0),'Hpal',384,1024,0)
  call SetUnitAcquireRange(udg_T327Unit,0)
  call UnitAddAbility(udg_T327Unit,'AInv')
  set udg_T327Item=UnitAddItemById(udg_T327Unit,'stwp')
  set udg_T327Target=CreateUnit(Player(0),'htow',1600,1024,0)
  call T327Mark("created")
 elseif age==5 then
  set ok=UnitUseItemTarget(udg_T327Unit,udg_T327Item,udg_T327Target)
  if ok then
   call T327Mark("cast-accepted")
  else
   call T327Mark("cast-rejected")
  endif
 elseif age==6 and udg_T327Scene==1 then
  set ok=IssueImmediateOrder(udg_T327Unit,"stop")
  if ok then
   call T327Mark("early-stop-accepted")
  else
   call T327Mark("early-stop-rejected")
  endif
 elseif age==15 then
  if udg_T327Scene==2 then
   set ok=IssueImmediateOrder(udg_T327Unit,"stop")
  elseif udg_T327Scene==3 then
   set ok=IssuePointOrder(udg_T327Unit,"move",640,1024)
  elseif udg_T327Scene==4 then
   call PauseUnit(udg_T327Unit,true)
   set ok=true
  elseif udg_T327Scene==5 then
   call KillUnit(udg_T327Unit)
   set ok=true
  elseif udg_T327Scene==6 then
   call RemoveItem(udg_T327Item)
   set ok=true
  elseif udg_T327Scene==7 then
   call RemoveUnit(udg_T327Target)
   set ok=true
  elseif udg_T327Scene==8 then
   call RemoveUnit(udg_T327Unit)
   set ok=true
  elseif udg_T327Scene==9 then
   set ok=UnitUseItemTarget(udg_T327Unit,udg_T327Item,udg_T327Target)
  endif
  if ok then
   call T327Mark("action-accepted")
  else
   call T327Mark("action-rejected")
  endif
 elseif age==70 and udg_T327Scene==4 then
  call PauseUnit(udg_T327Unit,false)
  call T327Mark("unpause")
 elseif age==97 then
  if udg_T327Scene!=8 then
   call RemoveUnit(udg_T327Unit)
  endif
  if udg_T327Scene!=7 and udg_T327Scene!=8 then
   call RemoveUnit(udg_T327Target)
  endif
  if udg_T327Scene!=6 and udg_T327Scene!=8 then
   call RemoveItem(udg_T327Item)
  endif
  call T327Mark("cleanup")
 elseif age==99 then
  if udg_T327Scene==9 then
   call T327Mark("complete")
   call PauseTimer(udg_T327Timer)
  endif
  set udg_T327Scene=udg_T327Scene+1
 endif
 if age<97 then
  call T327Mark("sample")
 endif
 set udg_T327Tick=udg_T327Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1327)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call T327Mark("start")
 set udg_T327Timer=CreateTimer()
 call TimerStart(udg_T327Timer,0.1,true,function T327Tick)
endfunction
