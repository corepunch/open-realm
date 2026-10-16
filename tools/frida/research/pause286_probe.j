globals
 unit udg_P286Unit=null
 unit udg_P286Target=null
 timer udg_P286Timer=null
 integer udg_P286Tick=0
 integer udg_P286Scene=0
 boolean udg_P286Record=false
endglobals
function P286Mark takes string label returns nothing
 call Preload("P286 tick="+I2S(udg_P286Tick)+" scene="+I2S(udg_P286Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_P286Unit))+" x="+R2S(GetUnitX(udg_P286Unit))+" y="+R2S(GetUnitY(udg_P286Unit)))
endfunction
function P286Issued takes nothing returns nothing
 if udg_P286Record and GetTriggerUnit()==udg_P286Unit then
  call P286Mark("issued-"+I2S(GetIssuedOrderId()))
 endif
endfunction
function P286UnitIssued takes nothing returns nothing
 if udg_P286Record then
  call P286Mark("unit-"+I2S(GetIssuedOrderId()))
 endif
endfunction
function P286Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_P286Tick,10)
 local integer action=ModuloInteger(udg_P286Scene,7)
 local trigger t=null
 local boolean accepted=false
 if phase==0 then
  set udg_P286Record=false
  set udg_P286Unit=CreateUnit(Player(0),'hfoo',656,656,0)
  set udg_P286Target=CreateUnit(Player(1),'hfoo',1488,1040,0)
  call SetUnitAcquireRange(udg_P286Unit,0)
  call SetUnitAcquireRange(udg_P286Target,0)
  call SetUnitInvulnerable(udg_P286Target,true)
  set t=CreateTrigger()
  call TriggerRegisterUnitEvent(t,udg_P286Unit,EVENT_UNIT_ISSUED_ORDER)
  call TriggerRegisterUnitEvent(t,udg_P286Unit,EVENT_UNIT_ISSUED_POINT_ORDER)
  call TriggerRegisterUnitEvent(t,udg_P286Unit,EVENT_UNIT_ISSUED_TARGET_ORDER)
  call TriggerAddAction(t,function P286UnitIssued)
  if udg_P286Scene>=7 then
   call IssuePointOrder(udg_P286Unit,"move",1280,656)
  endif
  call P286Mark("scene-start")
 elseif phase==1 then
  set udg_P286Record=true
  call P286Mark("before")
  call PauseUnit(udg_P286Unit,true)
  call SetUnitX(udg_P286Unit,656)
  call SetUnitY(udg_P286Unit,656)
  call P286Mark("paused")
  set accepted=IssuePointOrder(udg_P286Unit,"move",1280,656)
  if accepted then
   call P286Mark("first-true")
  else
   call P286Mark("first-false")
  endif
 elseif phase==2 then
  if action==0 then
   set accepted=IssueImmediateOrder(udg_P286Unit,"stop")
  elseif action==1 then
   set accepted=IssueImmediateOrder(udg_P286Unit,"holdposition")
  elseif action==2 then
   set accepted=IssuePointOrder(udg_P286Unit,"move",1040,1040)
  elseif action==3 then
   set accepted=IssueTargetOrder(udg_P286Unit,"move",udg_P286Target)
  elseif action==4 then
   set accepted=IssuePointOrder(udg_P286Unit,"attack",1040,1040)
  elseif action==5 then
   call SetUnitInvulnerable(udg_P286Target,false)
   set accepted=IssueTargetOrder(udg_P286Unit,"attack",udg_P286Target)
  else
   set accepted=IssuePointOrder(udg_P286Unit,"patrol",1040,1040)
  endif
  if accepted then
   call P286Mark("replace-true")
  else
   call P286Mark("replace-false")
  endif
 elseif phase==4 then
  call P286Mark("waiting")
  call PauseUnit(udg_P286Unit,false)
  call P286Mark("unpause-return")
 elseif phase==5 then
  call P286Mark("resumed")
 elseif phase==8 then
  call P286Mark("settled")
  set udg_P286Record=false
  call RemoveUnit(udg_P286Unit)
  call RemoveUnit(udg_P286Target)
 elseif phase==9 then
  set udg_P286Scene=udg_P286Scene+1
  if udg_P286Scene==14 then
   call P286Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_P286Timer)
  endif
 endif
 set udg_P286Tick=udg_P286Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerAddAction(t,function P286Issued)
 set udg_P286Timer=CreateTimer()
 call TimerStart(udg_P286Timer,0.1,true,function P286Tick)
 call P286Mark("start")
endfunction
