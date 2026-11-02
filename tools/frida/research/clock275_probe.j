globals
 unit udg_C275Unit=null
 timer udg_C275Timer=null
 integer udg_C275Tick=0
 integer udg_C275Speed=0
endglobals
function C275Mark takes string label returns nothing
 call Preload("C275 tick="+I2S(udg_C275Tick)+" label="+label+" selected="+I2S(udg_C275Speed)+" speed="+I2S(GetHandleId(GetGameSpeed()))+" x="+R2S(GetUnitX(udg_C275Unit))+" y="+R2S(GetUnitY(udg_C275Unit))+" order="+I2S(GetUnitCurrentOrder(udg_C275Unit)))
endfunction
function C275Tick takes nothing returns nothing
 set udg_C275Tick=udg_C275Tick+1
 if udg_C275Tick==1 then
  call PreloadGenEnd("clock275-started.txt")
 endif
 if ModuloInteger(udg_C275Tick,20)==0 and udg_C275Tick<100 then
  set udg_C275Speed=udg_C275Speed+1
  call SetGameSpeed(ConvertGameSpeed(udg_C275Speed))
  call SetUnitX(udg_C275Unit,256.)
  call IssuePointOrder(udg_C275Unit,"move",1800.,256.)
  call C275Mark("speed")
 else
  call C275Mark("beat")
 endif
 if udg_C275Tick==50 then
  call PauseGame(true)
  call C275Mark("script-paused")
  call PauseGame(false)
  call C275Mark("paused")
  call PreloadGenEnd("clock275-paused.txt")
 endif
 if udg_C275Tick==100 then
  call C275Mark("complete")
  call PreloadGenEnd("clock275.txt")
  call PauseTimer(udg_C275Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_C275Unit=CreateUnit(Player(0),'hfoo',256.,256.,0.)
 call UnitRemoveAbility(udg_C275Unit,'Aatk')
 call SetUnitMoveSpeed(udg_C275Unit,100.)
 call SetGameSpeed(ConvertGameSpeed(0))
 call IssuePointOrder(udg_C275Unit,"move",1800.,256.)
 call Preload("C275 tick=0 label=start")
 set udg_C275Timer=CreateTimer()
 call TimerStart(udg_C275Timer,.1,true,function C275Tick)
endfunction
