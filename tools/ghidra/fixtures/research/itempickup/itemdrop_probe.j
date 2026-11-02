globals
 timer pickTimer=null
 unit pickActor=null
 unit pickOther=null
 item pickItem=null
 integer pickTick=0
 integer pickScene=0
 boolean pickRecording=false
 boolean pickNested=false
endglobals
function PickBool takes boolean value returns integer
 if value then
  return 1
 endif
 return 0
endfunction
function PickMark takes string label,boolean accepted returns nothing
 call Preload("TPK tick="+I2S(pickTick)+" scene="+I2S(pickScene)+" label="+label+" accepted="+I2S(PickBool(accepted))+" head="+I2S(GetUnitCurrentOrder(pickActor))+" x="+R2S(GetUnitX(pickActor))+" y="+R2S(GetUnitY(pickActor))+" owned="+I2S(GetItemTypeId(UnitItemInSlot(pickActor,0)))+" anyowned="+I2S(PickBool(IsItemOwned(pickItem)))+" visible="+I2S(PickBool(IsItemVisible(pickItem)))+" ix="+R2S(GetItemX(pickItem))+" iy="+R2S(GetItemY(pickItem))+" itype="+I2S(GetItemTypeId(pickItem)))
endfunction
function PickFill takes nothing returns nothing
 local integer i=0
 loop
  exitwhen i==6
  call UnitAddItem(pickActor,CreateItem('ratf',704.0,1056.0))
  set i=i+1
 endloop
endfunction
function PickEvent takes string kind returns nothing
 if not pickRecording or GetTriggerUnit()!=pickActor then
  return
 endif
 call Preload("TPK tick="+I2S(pickTick)+" scene="+I2S(pickScene)+" label=event kind="+kind+" head="+I2S(GetUnitCurrentOrder(pickActor))+" issued="+I2S(GetIssuedOrderId())+" manip="+I2S(GetItemTypeId(GetManipulatedItem()))+" slot="+I2S(GetItemTypeId(UnitItemInSlot(pickActor,0))))
 if kind=="pickup" and GetManipulatedItem()==pickItem and not pickNested then
  set pickNested=true
  if pickScene==12 then
   call PickMark("nested-move",IssuePointOrder(pickActor,"move",512.0,1056.0))
  elseif pickScene==13 then
   call RemoveItem(pickItem)
   call PickMark("nested-remove",true)
  endif
 endif
endfunction
function PickPickup takes nothing returns nothing
 call PickEvent("pickup")
endfunction
function PickTarget takes nothing returns nothing
 call PickEvent("target")
endfunction
function PickPoint takes nothing returns nothing
 call PickEvent("point")
endfunction
function PickImmediate takes nothing returns nothing
 call PickEvent("immediate")
endfunction
function PickTick takes nothing returns nothing
 local integer age=ModuloInteger(pickTick,100)
 local real x=1104.0
 if age==0 then
  set pickRecording=false
  set pickNested=false
  if pickScene==0 or pickScene==5 then
   set x=804.0
  elseif pickScene>=2 and pickScene<=4 then
   set x=704.0+PICKUP_RANGE+33.0+(I2R(pickScene)-3.0)*0.25
  endif
  if pickScene==5 or pickScene==6 then
   set pickActor=CreateUnit(Player(0),'hhou',704.0,1024.0,0.0)
   call UnitAddAbility(pickActor,'AInv')
   set pickItem=CreateItem('ratf',x,1024.0)
  else
   set pickActor=CreateUnit(Player(0),'Hpal',704.0,1056.0,0.0)
   set pickItem=CreateItem('ratf',x,1056.0)
  endif
  call SetUnitAcquireRange(pickActor,0.0)
  call SetUnitMoveSpeed(pickActor,270.0)
  if pickScene==16 then
   call PickFill()
   call IssuePointOrder(pickActor,"move",512.0,1056.0)
  elseif pickScene==17 then
   call PauseUnit(pickActor,true)
  elseif pickScene==18 then
   call KillUnit(pickActor)
  endif
  call PickMark("spawn",true)
 elseif age==2 then
  set pickRecording=true
  call PickMark("before",true)
  call PickMark("smart-return",IssueTargetOrder(pickActor,"smart",pickItem))
 elseif age==5 then
  if pickScene==7 then
   call RemoveItem(pickItem)
   call PickMark("remove",true)
  elseif pickScene==8 then
   call PickMark("stop",IssueImmediateOrder(pickActor,"stop"))
  elseif pickScene==9 then
   call PauseUnit(pickActor,true)
   call PickMark("pause",true)
  elseif pickScene==10 then
   call PickMark("inventory-remove",UnitRemoveAbility(pickActor,'AInv'))
  elseif pickScene==11 then
   call PickFill()
   call PickMark("fill",true)
  elseif pickScene==14 then
   set pickOther=CreateUnit(Player(0),'Hpal',1500.0,1056.0,0.0)
   call PickMark("other-pickup",UnitAddItem(pickOther,pickItem))
  elseif pickScene==15 then
   call SetItemVisible(pickItem,false)
   call PickMark("hide",true)
  endif
 elseif age==20 and pickScene==9 then
  call PauseUnit(pickActor,false)
  call PickMark("unpause",true)
 elseif age==99 then
  call PickMark("settled",true)
  set pickRecording=false
  call RemoveItem(pickItem)
  call RemoveUnit(pickActor)
  if pickOther!=null then
   call RemoveUnit(pickOther)
   set pickOther=null
  endif
  set pickScene=pickScene+1
  if pickScene==19 then
   call PickMark("complete",true)
   call DestroyTimer(pickTimer)
   call PreloadGenEnd("PRELOAD_FILE")
   return
  endif
 elseif ModuloInteger(age,10)==0 then
  call PickMark("sample",true)
 endif
 set pickTick=pickTick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger pickup=CreateTrigger()
 local trigger target=CreateTrigger()
 local trigger point=CreateTrigger()
 local trigger immediate=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(pickup,Player(0),EVENT_PLAYER_UNIT_PICKUP_ITEM,null)
 call TriggerRegisterPlayerUnitEvent(target,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(point,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(immediate,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
 call TriggerAddAction(pickup,function PickPickup)
 call TriggerAddAction(target,function PickTarget)
 call TriggerAddAction(point,function PickPoint)
 call TriggerAddAction(immediate,function PickImmediate)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call Preload("TPK tick=0 scene=-1 label=start")
 call PreloadGenEnd("START_FILE")
 call PreloadGenClear()
 call PreloadGenStart()
 set pickTimer=CreateTimer()
 call TimerStart(pickTimer,0.02,true,function PickTick)
endfunction
