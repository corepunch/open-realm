globals
 unit udg_O303Unit=null
 timer udg_O303Timer=null
 integer udg_O303Tick=0
 integer udg_O303Scene=0
endglobals
function O303Mark takes string label returns nothing
 call Preload("O303 tick="+I2S(udg_O303Tick)+" scene="+I2S(udg_O303Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_O303Unit))+" order="+I2S(GetUnitCurrentOrder(udg_O303Unit))+" life="+R2S(GetWidgetLife(udg_O303Unit))+" speed="+R2S(GetUnitMoveSpeed(udg_O303Unit))+" x="+R2S(GetUnitX(udg_O303Unit))+" y="+R2S(GetUnitY(udg_O303Unit)))
endfunction
function O303Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_O303Tick,6)
 local integer kind=ModuloInteger(udg_O303Scene,4)
 local integer unitCode='h303'
 if kind==1 then
  set unitCode='i303'
 elseif kind==2 then
  set unitCode='f303'
 elseif kind==3 then
  set unitCode='htow'
 endif
 if phase==0 then
  call O303Mark("scene-start")
  set udg_O303Unit=CreateUnit(Player(0),unitCode,656,656,0)
  call SetUnitAcquireRange(udg_O303Unit,0)
  call O303Mark("created")
 elseif phase==1 then
  call O303Mark("before")
  if udg_O303Scene<4 then
   call KillUnit(udg_O303Unit)
  else
   call RemoveUnit(udg_O303Unit)
  endif
  call O303Mark("after")
 elseif phase==3 then
  call O303Mark("settled")
  if udg_O303Scene<4 then
   call RemoveUnit(udg_O303Unit)
  endif
 elseif phase==5 then
  set udg_O303Scene=udg_O303Scene+1
  if udg_O303Scene==8 then
   call O303Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_O303Timer)
  endif
 endif
 set udg_O303Tick=udg_O303Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 set udg_O303Timer=CreateTimer()
 call TimerStart(udg_O303Timer,0.1,true,function O303Tick)
 call O303Mark("start")
endfunction
