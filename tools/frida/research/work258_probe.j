globals
 unit udg_W258Unit=null
 timer udg_W258Timer=null
 integer udg_W258Tick=0
 integer udg_W258Scene=0
endglobals
function W258Mark takes string label returns nothing
 call Preload("W258 tick="+I2S(udg_W258Tick)+" scene="+I2S(udg_W258Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_W258Unit))+" x="+R2S(GetUnitX(udg_W258Unit))+" y="+R2S(GetUnitY(udg_W258Unit)))
endfunction
function W258Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_W258Tick,50)
 local integer typ='hfoo'
 local boolean result=false
 if phase==0 then
  if udg_W258Scene==1 then
   set typ='hbar'
  elseif udg_W258Scene>=2 then
   set typ='etol'
  endif
  set udg_W258Unit=CreateUnit(Player(0),typ,1024,1024,0)
  call SetUnitAcquireRange(udg_W258Unit,0)
  if udg_W258Scene==3 then
   set result=IssueImmediateOrder(udg_W258Unit,"unroot")
   call W258Mark("unroot-"+I2S(GetUnitCurrentOrder(udg_W258Unit)))
  endif
 elseif phase==40 then
  call W258Mark("before")
  set result=IssueImmediateOrder(udg_W258Unit,"stop")
  if result then
   call W258Mark("after-accepted")
  else
   call W258Mark("after-rejected")
  endif
 elseif phase==44 then
  call W258Mark("settled")
  call RemoveUnit(udg_W258Unit)
 elseif phase==49 then
  set udg_W258Scene=udg_W258Scene+1
  if udg_W258Scene==4 then
   call W258Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_W258Timer)
  endif
 endif
 set udg_W258Tick=udg_W258Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 set udg_W258Timer=CreateTimer()
 call TimerStart(udg_W258Timer,0.1,true,function W258Tick)
 call Preload("W258 tick=0 scene=0 label=start order=0")
endfunction
