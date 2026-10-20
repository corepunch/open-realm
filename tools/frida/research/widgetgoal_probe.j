globals
 timer udg_WGTimer=null
 integer udg_WGTick=0
 integer udg_WGScene=-1
 integer udg_WGLocal=0
 integer udg_WGIdle=0
 unit udg_WGUnit=null
 destructable udg_WGBridge=null
endglobals
function WGMark takes string label returns nothing
 call Preload("WG tick="+I2S(udg_WGTick)+" scene="+I2S(udg_WGScene)+" l="+I2S(udg_WGLocal)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_WGUnit))+" x="+R2SW(GetUnitX(udg_WGUnit),1,4)+" y="+R2SW(GetUnitY(udg_WGUnit),1,4)+" life="+R2SW(GetDestructableLife(udg_WGBridge),1,1))
endfunction
function WGSetup takes nothing returns nothing
 call WGMark("begin-setup")
 set udg_WGUnit=CreateUnit(Player(0),'hQ0A'+ModuloInteger(udg_WGScene,20),576,640,0)
 call SetUnitAcquireRange(udg_WGUnit,0)
 call WGMark("created")
 if IssueTargetOrder(udg_WGUnit,"move",udg_WGBridge) then
  call WGMark("accepted")
 else
  call WGMark("rejected")
 endif
endfunction
function WGTick takes nothing returns nothing
 set udg_WGTick=udg_WGTick+1
 if udg_WGTick==1 then
  call WGMark("start")
  call PreloadGenEnd("widgetgoal-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 endif
 if udg_WGTick==20 then
  set udg_WGScene=0
  call WGSetup()
 elseif udg_WGScene>=0 then
  set udg_WGLocal=udg_WGLocal+1
  if udg_WGLocal==5 then
   call WGMark("before-removal")
   if udg_WGScene<20 then
    call KillDestructable(udg_WGBridge)
   else
    call RemoveDestructable(udg_WGBridge)
    set udg_WGBridge=null
   endif
   call WGMark("after-removal")
  endif
  call WGMark("sample")
  if GetUnitCurrentOrder(udg_WGUnit)==0 then
   set udg_WGIdle=udg_WGIdle+1
  else
   set udg_WGIdle=0
  endif
  if udg_WGLocal==80 or udg_WGIdle==5 then
   call WGMark("begin-cleanup")
   call RemoveUnit(udg_WGUnit)
   set udg_WGUnit=null
   if udg_WGScene<20 then
    call DestructableRestoreLife(udg_WGBridge,10000,false)
   else
    set udg_WGBridge=CreateDestructableZ('LT06',1024,640,0,0,1,0)
   endif
   call WGMark("end-cleanup")
   set udg_WGLocal=0
   set udg_WGIdle=0
   set udg_WGScene=udg_WGScene+1
   if udg_WGScene==40 then
    call WGMark("complete")
    call PauseTimer(udg_WGTimer)
    call PreloadGenEnd("@OUTPUT@")
   else
    call WGSetup()
   endif
  endif
 endif
endfunction
function WGFindBridge takes nothing returns nothing
 if GetDestructableTypeId(GetEnumDestructable())=='LT06' then
  set udg_WGBridge=GetEnumDestructable()
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call EnumDestructablesInRect(Rect(0,0,2048,2048),null,function WGFindBridge)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_WGTimer=CreateTimer()
 call TimerStart(udg_WGTimer,0.1,true,function WGTick)
endfunction
