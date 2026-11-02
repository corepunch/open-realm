globals
 unit udg_R304Unit=null
 timer udg_R304Timer=null
 integer udg_R304Tick=0
 integer udg_R304Scene=0
 boolean udg_R304Nested=false
endglobals
function R304Mark takes string label returns nothing
 call Preload("R304 tick="+I2S(udg_R304Tick)+" scene="+I2S(udg_R304Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_R304Unit))+" order="+I2S(GetUnitCurrentOrder(udg_R304Unit))+" life="+R2S(GetWidgetLife(udg_R304Unit))+" x="+R2S(GetUnitX(udg_R304Unit))+" y="+R2S(GetUnitY(udg_R304Unit)))
endfunction
function R304Issued takes nothing returns nothing
 if udg_R304Scene>=4 and GetIssuedOrderId()==OrderId("stop") and not udg_R304Nested then
  set udg_R304Nested=true
  call R304Mark("nested-before")
  call RemoveUnit(udg_R304Unit)
  call R304Mark("nested-after")
 endif
endfunction
function R304Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_R304Tick,6)
 local trigger t
 if phase==0 then
  call R304Mark("scene-start")
  set udg_R304Nested=false
  set udg_R304Unit=CreateUnit(Player(0),'h303',656,656,0)
  call SetUnitAcquireRange(udg_R304Unit,0)
  set t=CreateTrigger()
  call TriggerRegisterUnitEvent(t,udg_R304Unit,EVENT_UNIT_ISSUED_ORDER)
  call TriggerAddAction(t,function R304Issued)
  if ModuloInteger(udg_R304Scene,2)==1 then
   call IssuePointOrder(udg_R304Unit,"move",1200,656)
  endif
  call R304Mark("created")
 elseif phase==1 then
  if udg_R304Scene==2 or udg_R304Scene==3 then
   call PauseUnit(udg_R304Unit,true)
  endif
 elseif phase==2 then
  call R304Mark("before")
  if udg_R304Scene>=4 then
   call IssueImmediateOrder(udg_R304Unit,"stop")
  else
   call RemoveUnit(udg_R304Unit)
  endif
  call R304Mark("after")
 elseif phase==4 then
  call R304Mark("settled")
 elseif phase==5 then
  set udg_R304Scene=udg_R304Scene+1
  if udg_R304Scene==6 then
   call R304Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_R304Timer)
  endif
 endif
 set udg_R304Tick=udg_R304Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 set udg_R304Timer=CreateTimer()
 call TimerStart(udg_R304Timer,0.1,true,function R304Tick)
 call R304Mark("start")
endfunction
