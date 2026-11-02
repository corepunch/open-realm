globals
 timer udg_T377Timer=null
 unit udg_T377Unit=null
 unit udg_T377Gate=null
 integer udg_T377Tick=0
 integer udg_T377Scene=0
endglobals
function T377Mark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer active=0
 if ok then
  set accepted=1
 endif
 if WaygateIsActive(udg_T377Gate) then
  set active=1
 endif
 call Preload("T377 tick="+I2S(udg_T377Tick)+" scene="+I2S(udg_T377Scene)+" label="+label+" unitid="+I2S(GetUnitTypeId(udg_T377Unit))+" gateid="+I2S(GetUnitTypeId(udg_T377Gate))+" accepted="+I2S(accepted)+" x="+R2S(GetUnitX(udg_T377Unit))+" y="+R2S(GetUnitY(udg_T377Unit))+" order="+I2S(GetUnitCurrentOrder(udg_T377Unit))+" active="+I2S(active)+" ability="+I2S(GetUnitAbilityLevel(udg_T377Gate,'Awrp'))+" gatex="+R2S(GetUnitX(udg_T377Gate))+" gatey="+R2S(GetUnitY(udg_T377Gate))+" destx="+R2S(WaygateGetDestinationX(udg_T377Gate))+" desty="+R2S(WaygateGetDestinationY(udg_T377Gate)))
endfunction
function T377Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_T377Tick,120)
 local string order="move"
 if age==0 then
  set udg_T377Gate=CreateUnit(Player(0),'nwgt',768.0,304.0,0.0)
  call WaygateSetDestination(udg_T377Gate,1536.0,304.0)
  call WaygateActivate(udg_T377Gate,udg_T377Scene!=1)
  set udg_T377Unit=CreateUnit(Player(0),'hfoo',272.0,304.0,0.0)
  call SetUnitAcquireRange(udg_T377Unit,0.0)
  call T377Mark("created",udg_T377Unit!=null and udg_T377Gate!=null and GetUnitAbilityLevel(udg_T377Gate,'Awrp')>0)
 elseif age==10 then
  if udg_T377Scene==3 then
   set order="smart"
  endif
  call T377Mark("issue-before",true)
  call T377Mark("issue-after",IssueTargetOrder(udg_T377Unit,order,udg_T377Gate))
 elseif age==15 and udg_T377Scene==2 then
  call T377Mark("retarget-before",true)
  call WaygateSetDestination(udg_T377Gate,1536.0,768.0)
  call T377Mark("retarget-after",true)
 elseif age==110 then
  call T377Mark("terminal",true)
  call RemoveUnit(udg_T377Unit)
  call RemoveUnit(udg_T377Gate)
  set udg_T377Unit=null
  set udg_T377Gate=null
  call T377Mark("cleaned",true)
 elseif age==119 then
  if udg_T377Scene==3 then
   call T377Mark("complete",true)
   call PreloadGenEnd("target377-waygate-move.txt")
   call PauseTimer(udg_T377Timer)
  endif
  set udg_T377Scene=udg_T377Scene+1
 endif
 if age<110 then
  call T377Mark("sample",true)
 endif
 set udg_T377Tick=udg_T377Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1377)
 call FogEnable(false)
 call FogMaskEnable(false)
 call Preload("T377 tick=0 label=start")
 call PreloadGenEnd("target377-waygate-move-start.txt")
 set udg_T377Timer=CreateTimer()
 call TimerStart(udg_T377Timer,0.1,true,function T377Tick)
endfunction
