globals
 timer tdaTimer=null
 unit tdaActor=null
 unit tdaFriend=null
 item tdaItem=null
 item tdaAlt=null
 integer tdaTick=0
 integer tdaScene=0
 integer tdaEvents=0
 boolean tdaRecording=false
 boolean tdaNested=false
endglobals
function TdaMark takes string label,boolean accepted returns nothing
 local integer ok=0
 local integer owned=0
 if accepted then
  set ok=1
 endif
 if IsItemOwned(tdaItem) then
  set owned=1
 endif
 call Preload("TDA tick="+I2S(tdaTick)+" scene="+I2S(tdaScene)+" label="+label+" accepted="+I2S(ok)+" head="+I2S(GetUnitCurrentOrder(tdaActor))+" type="+I2S(GetUnitTypeId(tdaActor))+" x="+R2S(GetUnitX(tdaActor))+" y="+R2S(GetUnitY(tdaActor))+" owned="+I2S(owned)+" altowned="+I2S(GetItemTypeId(UnitItemInSlot(tdaActor,1)))+" events="+I2S(tdaEvents))
endfunction
function TdaEvent takes string kind returns nothing
 if not tdaRecording then
  return
 endif
 set tdaEvents=tdaEvents+1
 call Preload("TDA tick="+I2S(tdaTick)+" scene="+I2S(tdaScene)+" label=event kind="+kind+" order="+I2S(GetIssuedOrderId())+" head="+I2S(GetUnitCurrentOrder(tdaActor))+" ox="+R2S(GetOrderPointX())+" oy="+R2S(GetOrderPointY())+" targetitem="+I2S(GetItemTypeId(GetOrderTargetItem()))+" targetunit="+I2S(GetUnitTypeId(GetOrderTargetUnit())))
 if GetIssuedOrderId()==852001 and not tdaNested then
  set tdaNested=true
  if tdaScene==6 then
   call TdaMark("nested-move",IssuePointOrder(tdaActor,"move",512.0,1056.0))
  elseif tdaScene==7 then
   call RemoveItem(tdaItem)
   set tdaItem=null
   call TdaMark("nested-remove-item",true)
  elseif tdaScene==8 then
   call RemoveUnit(tdaActor)
   call TdaMark("nested-remove-unit",true)
  elseif tdaScene==9 then
   call TdaMark("nested-drop",UnitDropItemPoint(tdaActor,tdaAlt,1400.0,1056.0))
  elseif tdaScene==11 then
   call UnitRemoveItem(tdaActor,tdaItem)
   call TdaMark("nested-unequip",true)
  endif
 endif
endfunction
function TdaImmediate takes nothing returns nothing
 call TdaEvent("immediate")
endfunction
function TdaPoint takes nothing returns nothing
 call TdaEvent("point")
endfunction
function TdaTarget takes nothing returns nothing
 call TdaEvent("target")
endfunction
function TdaTick takes nothing returns nothing
 local integer age=ModuloInteger(tdaTick,20)
 if age==0 then
  set tdaRecording=false
  set tdaNested=false
  set tdaEvents=0
  if tdaScene==6 or tdaScene==7 then
   set tdaActor=CreateUnit(Player(0),'hhou',704.0,1056.0,0.0)
   call UnitAddAbility(tdaActor,'AInv')
  else
   set tdaActor=CreateUnit(Player(0),'Hpal',704.0,1056.0,0.0)
  endif
  call SetUnitAcquireRange(tdaActor,0.0)
  call SetUnitMoveSpeed(tdaActor,270.0)
  set tdaFriend=CreateUnit(Player(0),'hfoo',1500.0,1056.0,0.0)
  call SetUnitAcquireRange(tdaFriend,0.0)
  set tdaItem=CreateItem('ratf',704.0,1056.0)
  if tdaScene!=3 then
   call UnitAddItem(tdaActor,tdaItem)
  endif
  if tdaScene==9 then
   set tdaAlt=CreateItem('ratf',704.0,1056.0)
   call UnitAddItem(tdaActor,tdaAlt)
  endif
  if tdaScene==2 then
   call SetItemDroppable(tdaItem,false)
  endif
  if tdaScene==8 then
   call KillUnit(tdaActor)
  elseif tdaScene==9 then
   call SetUnitMoveSpeed(tdaActor,0.0)
  endif
  if tdaScene==4 then
   call IssueImmediateOrder(tdaActor,"stop")
  elseif tdaScene==5 then
   call IssuePointOrder(tdaActor,"move",1900.0,1056.0)
  elseif tdaScene==10 then
   call PauseUnit(tdaActor,true)
  endif
 elseif age==2 then
  set tdaRecording=true
  call TdaMark("before",true)
  if tdaScene==6 then
   call TdaMark("drop-return",UnitDropItemPoint(tdaActor,tdaItem,GetUnitX(tdaActor)+99.75,GetUnitY(tdaActor)))
  elseif tdaScene==0 then
   call TdaMark("drop-return",UnitDropItemPoint(tdaActor,tdaItem,803.75,1056.0))
  else
   call TdaMark("drop-return",UnitDropItemPoint(tdaActor,tdaItem,1104.0,1056.0))
  endif
  call TdaMark("after",true)
 elseif age==3 then
  if tdaScene==0 then
   call TdaMark("control-point",IssuePointOrder(tdaActor,"move",512.0,1056.0))
  elseif tdaScene==1 then
   call TdaMark("control-target",IssueTargetOrder(tdaActor,"move",tdaFriend))
  elseif tdaScene==2 then
   call TdaMark("control-immediate",IssueImmediateOrder(tdaActor,"stop"))
  endif
 elseif age==5 and tdaScene==10 then
  call PauseUnit(tdaActor,false)
  call TdaMark("unpause",true)
 elseif age==18 then
  call TdaMark("settled",true)
  set tdaRecording=false
  call RemoveItem(tdaItem)
  call RemoveItem(tdaAlt)
  call RemoveUnit(tdaActor)
  call RemoveUnit(tdaFriend)
  set tdaFriend=null
  set tdaItem=null
  set tdaAlt=null
  set tdaActor=null
 elseif age==19 then
  set tdaScene=tdaScene+1
  if tdaScene==12 then
   call PauseTimer(tdaTimer)
   call TdaMark("complete",true)
   call PreloadGenEnd("PRELOAD_FILE")
  endif
 endif
 if age<18 then
  call TdaMark("sample",true)
 endif
 set tdaTick=tdaTick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger immediate=CreateTrigger()
 local trigger point=CreateTrigger()
 local trigger target=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(immediate,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(point,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(target,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerAddAction(immediate,function TdaImmediate)
 call TriggerAddAction(point,function TdaPoint)
 call TriggerAddAction(target,function TdaTarget)
 call SetRandomSeed(38301)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SuspendTimeOfDay(true)
 call PreloadGenClear()
 call PreloadGenStart()
 call Preload("TDA tick=0 scene=-1 label=start")
 call PreloadGenEnd("START_FILE")
 set tdaTimer=CreateTimer()
 call TimerStart(tdaTimer,0.1,true,function TdaTick)
endfunction
