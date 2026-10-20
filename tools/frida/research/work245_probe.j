globals
 unit udg_W245F=null
 unit udg_W245T=null
 timer udg_W245Timer=null
 integer udg_W245Tick=0
 integer udg_W245Scene=0
endglobals
function W245Mark takes string label returns nothing
 call Preload("W245 tick="+I2S(udg_W245Tick)+" scene="+I2S(udg_W245Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_W245F))+" x="+R2S(GetUnitX(udg_W245F))+" y="+R2S(GetUnitY(udg_W245F))+" facing="+R2S(GetUnitFacing(udg_W245F))+" life="+R2S(GetWidgetLife(udg_W245T)))
endfunction
function W245Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_W245Tick,40)
 if t==0 then
  set udg_W245F=CreateUnit(Player(0),'hfoo',800,800,180)
  set udg_W245T=CreateUnit(Player(1),'hfoo',900,800,0)
  call SetUnitAcquireRange(udg_W245F,0)
  call SetUnitAcquireRange(udg_W245T,0)
  call PauseUnit(udg_W245T,true)
  if udg_W245Scene==1 then
   call SetUnitTurnSpeed(udg_W245F,0.05)
  elseif udg_W245Scene==2 then
   call SetUnitFacing(udg_W245F,5)
  endif
  call W245Mark("begin")
 elseif t==1 then
  if IssueTargetOrder(udg_W245F,"attack",udg_W245T) then
   call W245Mark("accepted")
  else
   call W245Mark("rejected")
  endif
 elseif t==30 then
  call IssueImmediateOrder(udg_W245F,"stop")
  call W245Mark("stopped")
 elseif t==35 then
  call RemoveUnit(udg_W245F)
  call RemoveUnit(udg_W245T)
 elseif t==39 then
  if udg_W245Scene==2 then
   call W245Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_W245Timer)
  endif
  set udg_W245Scene=udg_W245Scene+1
 endif
 if t<35 then
  call W245Mark("sample")
 endif
 set udg_W245Tick=udg_W245Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 set udg_W245Timer=CreateTimer()
 call TimerStart(udg_W245Timer,0.1,true,function W245Tick)
 call Preload("W245 tick=0 scene=0 label=start order=0")
endfunction
