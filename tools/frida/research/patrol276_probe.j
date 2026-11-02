globals
 unit array udg_P276Unit
 group udg_P276Group=null
 timer udg_P276Timer=null
 integer udg_P276Tick=0
endglobals
function P276Mark takes string label returns nothing
 local integer i=0
 loop
  exitwhen i>3
  call Preload("P276 tick="+I2S(udg_P276Tick)+" label="+label+" unit="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_P276Unit[i]))+" x="+R2S(GetUnitX(udg_P276Unit[i]))+" y="+R2S(GetUnitY(udg_P276Unit[i])))
  set i=i+1
 endloop
endfunction
function P276Tick takes nothing returns nothing
 set udg_P276Tick=udg_P276Tick+1
 if udg_P276Tick==1 then
  call GroupPointOrder(udg_P276Group,"patrol",1200.,256.)
  call IssuePointOrder(udg_P276Unit[3],"move",640.,1024.)
 endif
 call P276Mark("beat")
 if udg_P276Tick==100 then
  call P276Mark("complete")
  call PreloadGenEnd("patrol276.txt")
  call PauseTimer(udg_P276Timer)
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local integer i=0
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_P276Group=CreateGroup()
 loop
  exitwhen i>3
  set udg_P276Unit[i]=CreateUnit(Player(0),'hfoo',256.,256.+I2R(i)*256.,0.)
  call SetUnitAcquireRange(udg_P276Unit[i],0.)
  if i<3 then
   call GroupAddUnit(udg_P276Group,udg_P276Unit[i])
  endif
  set i=i+1
 endloop
 call Preload("P276 tick=0 label=start")
 set udg_P276Timer=CreateTimer()
 call TimerStart(udg_P276Timer,.1,true,function P276Tick)
endfunction
