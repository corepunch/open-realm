globals
 timer giveTimer=null
 unit giveActor=null
 unit giveTarget=null
 widget giveWidget=null
 item giveOther=null
 destructable giveTree=null
 item giveItem=null
 integer giveTick=0
 integer giveScene=0
 boolean giveRecording=false
 boolean giveNested=false
endglobals
function GiveMark takes string label,boolean accepted returns nothing
 local integer ok=0
 local integer ownedAny=0
 local integer visible=0
 if accepted then
  set ok=1
 endif
 if IsItemVisible(giveItem) then
  set visible=1
 endif
 if IsItemOwned(giveItem) then
  set ownedAny=1
 endif
 call Preload("TGV tick="+I2S(giveTick)+" scene="+I2S(giveScene)+" label="+label+" accepted="+I2S(ok)+" head="+I2S(GetUnitCurrentOrder(giveActor))+" x="+R2S(GetUnitX(giveActor))+" y="+R2S(GetUnitY(giveActor))+" tx="+R2S(GetWidgetX(giveWidget))+" ty="+R2S(GetWidgetY(giveWidget))+" owned="+I2S(GetItemTypeId(UnitItemInSlot(giveActor,0)))+" received="+I2S(GetItemTypeId(UnitItemInSlot(giveTarget,0)))+" gold="+I2S(GetPlayerState(Player(0),PLAYER_STATE_RESOURCE_GOLD))+" gold1="+I2S(GetPlayerState(Player(1),PLAYER_STATE_RESOURCE_GOLD))+" itype="+I2S(GetItemTypeId(giveItem))+" anyowned="+I2S(ownedAny)+" visible="+I2S(visible)+" ix="+R2S(GetItemX(giveItem))+" iy="+R2S(GetItemY(giveItem)))
endfunction
function GiveBool takes boolean value returns integer
 if value then
  return 1
 endif
 return 0
endfunction
function GiveFill takes nothing returns nothing
 local integer i=0
 loop
  exitwhen i==6
  call UnitAddItem(giveTarget,CreateItem('ratf',1104.0,1056.0))
  set i=i+1
 endloop
endfunction
function GiveEvent takes string kind returns nothing
 local integer who=0
 local integer match=0
 if not giveRecording then
  return
 endif
 if GetTriggerUnit()==giveActor then
  set who=1
 elseif GetTriggerUnit()==giveTarget then
  set who=2
 else
  return
 endif
 if GetManipulatedItem()==giveItem or (kind=="pawn" and GetSoldItem()==giveItem) then
  set match=1
 endif
 call Preload("TGV tick="+I2S(giveTick)+" scene="+I2S(giveScene)+" label=event kind="+kind+" who="+I2S(who)+" match="+I2S(match)+" head="+I2S(GetUnitCurrentOrder(giveActor))+" issued="+I2S(GetIssuedOrderId())+" manip="+I2S(GetItemTypeId(GetManipulatedItem()))+" sold="+I2S(GetItemTypeId(GetSoldItem()))+" srcslot="+I2S(GetItemTypeId(UnitItemInSlot(giveActor,0)))+" dstslot="+I2S(GetItemTypeId(UnitItemInSlot(giveTarget,0))))
 if match==1 and not giveNested and ((kind=="pawn" and giveScene<4) or (kind=="drop" and giveScene>=4 and giveScene<=8)) then
  set giveNested=true
  if giveScene==8 or ModuloInteger(giveScene,4)==3 then
   call GiveMark("nested-move",IssuePointOrder(giveActor,"move",512.0,1056.0))
  elseif ModuloInteger(giveScene,4)==0 then
   call SetUnitOwner(giveActor,Player(1),false)
   call GiveMark("nested-owner",true)
  elseif ModuloInteger(giveScene,4)==1 then
   call RemoveItem(giveItem)
   call GiveMark("nested-remove-item",true)
  else
   call RemoveUnit(giveActor)
   call GiveMark("nested-remove-source",true)
  endif
 endif
endfunction
function GiveDropEvent takes nothing returns nothing
 call GiveEvent("drop")
endfunction
function GivePickupEvent takes nothing returns nothing
 call GiveEvent("pickup")
endfunction
function GivePawnEvent takes nothing returns nothing
 call GiveEvent("pawn")
endfunction
function GivePointEvent takes nothing returns nothing
 call GiveEvent("point")
endfunction
function GiveTargetEvent takes nothing returns nothing
 call GiveEvent("target")
endfunction
function GiveImmediateEvent takes nothing returns nothing
 call GiveEvent("immediate")
endfunction
function GiveTick takes nothing returns nothing
 local integer age=ModuloInteger(giveTick,200)
 if age==0 then
  set giveRecording=false
  set giveActor=CreateUnit(Player(0),'Hpal',704.0,1056.0,0.0)
  set giveTarget=CreateUnit(Player(0),'Hpal',843.0,1056.0,0.0)
  set giveWidget=giveTarget
  call SetUnitAcquireRange(giveActor,0.0)
  call SetUnitAcquireRange(giveTarget,0.0)
  set giveItem=CreateItem('ratf',900.0,1056.0)
  if giveScene==0 then
   call GiveMark("source-add",UnitAddItem(giveActor,giveItem))
  endif
  call GiveMark("spawn",true)
  call Preload("TGV tick="+I2S(giveTick)+" scene="+I2S(giveScene)+" label=capacities source="+I2S(UnitInventorySize(giveActor))+" target="+I2S(UnitInventorySize(giveTarget)))
 elseif age==2 then
  set giveRecording=true
  call GiveMark("before",true)
  if giveScene==0 then
   call GiveMark("give-return",UnitDropItemTarget(giveActor,giveItem,giveTarget))
  elseif giveScene==1 then
   call GiveMark("script-return",UnitAddItem(giveTarget,giveItem))
  else
   call GiveMark("smart-return",IssueTargetOrder(giveTarget,"smart",giveItem))
   call Preload("TGV tick="+I2S(giveTick)+" scene="+I2S(giveScene)+" label=recipient-head head="+I2S(GetUnitCurrentOrder(giveTarget)))
  endif
 elseif age==199 then
  call GiveMark("settled",true)
  set giveRecording=false
  call RemoveItem(giveItem)
  call RemoveUnit(giveActor)
  call RemoveUnit(giveTarget)
  set giveScene=giveScene+1
  if giveScene==3 then
   call GiveMark("complete",true)
   call DestroyTimer(giveTimer)
   call PreloadGenEnd("PRELOAD_FILE")
   return
  endif
 elseif ModuloInteger(age,10)==0 then
  call GiveMark("sample",true)
 endif
 set giveTick=giveTick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger drop=CreateTrigger()
 local trigger pickup=CreateTrigger()
 local trigger pawn=CreateTrigger()
 local trigger point=CreateTrigger()
 local trigger target=CreateTrigger()
 local trigger immediate=CreateTrigger()
 local integer p=0
 loop
  exitwhen p==16
  call TriggerRegisterPlayerUnitEvent(drop,Player(p),EVENT_PLAYER_UNIT_DROP_ITEM,null)
  call TriggerRegisterPlayerUnitEvent(pickup,Player(p),EVENT_PLAYER_UNIT_PICKUP_ITEM,null)
  call TriggerRegisterPlayerUnitEvent(pawn,Player(p),EVENT_PLAYER_UNIT_PAWN_ITEM,null)
  call TriggerRegisterPlayerUnitEvent(point,Player(p),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
  call TriggerRegisterPlayerUnitEvent(target,Player(p),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
  call TriggerRegisterPlayerUnitEvent(immediate,Player(p),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
  set p=p+1
 endloop
 call TriggerAddAction(drop,function GiveDropEvent)
 call TriggerAddAction(pickup,function GivePickupEvent)
 call TriggerAddAction(pawn,function GivePawnEvent)
 call TriggerAddAction(point,function GivePointEvent)
 call TriggerAddAction(target,function GiveTargetEvent)
 call TriggerAddAction(immediate,function GiveImmediateEvent)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call Preload("TGV tick=0 scene=-1 label=start")
 call PreloadGenEnd("START_FILE")
 call PreloadGenClear()
 call PreloadGenStart()
 set giveTimer=CreateTimer()
 call TimerStart(giveTimer,0.02,true,function GiveTick)
endfunction
