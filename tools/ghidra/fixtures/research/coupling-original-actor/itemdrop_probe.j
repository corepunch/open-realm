globals
 timer cpTimer=null
 unit cpArcher=null
 unit cpHippo=null
 integer cpTick=0
 integer cpScene=0
 boolean cpNested=false
endglobals
function CpBool takes boolean b returns integer
 if b then
  return 1
 endif
 return 0
endfunction
function CpMark takes string label,boolean accepted returns nothing
 local group g=CreateGroup()
 local unit u=null
 local integer riders=0
 local real rx=0.0
 local real ry=0.0
 call GroupEnumUnitsOfPlayer(g,Player(0),null)
 loop
  set u=FirstOfGroup(g)
  exitwhen u==null
  call GroupRemoveUnit(g,u)
  if GetUnitTypeId(u)=='ehpr' then
   set riders=riders+1
   set rx=GetUnitX(u)
   set ry=GetUnitY(u)
  endif
 endloop
 call DestroyGroup(g)
 call Preload("COUP tick="+I2S(cpTick)+" scene="+I2S(cpScene)+" label="+label+" accepted="+I2S(CpBool(accepted))+" range=333 reht="+I2S(GetPlayerTechCount(Player(0),'Reht',true))+" ah="+I2S(GetUnitCurrentOrder(cpArcher))+" at="+I2S(GetUnitTypeId(cpArcher))+" ax="+R2S(GetUnitX(cpArcher))+" ay="+R2S(GetUnitY(cpArcher))+" af="+R2S(GetUnitFacing(cpArcher))+" al="+R2S(GetWidgetLife(cpArcher))+" aa="+I2S(GetUnitAbilityLevel(cpArcher,'Acoa'))+" hh="+I2S(GetUnitCurrentOrder(cpHippo))+" ht="+I2S(GetUnitTypeId(cpHippo))+" hx="+R2S(GetUnitX(cpHippo))+" hy="+R2S(GetUnitY(cpHippo))+" hf="+R2S(GetUnitFacing(cpHippo))+" hl="+R2S(GetWidgetLife(cpHippo))+" ha="+I2S(GetUnitAbilityLevel(cpHippo,'Acoh'))+" riders="+I2S(riders)+" rx="+R2S(rx)+" ry="+R2S(ry))
endfunction
function CpEvent takes string kind returns nothing
 local unit actor=GetTriggerUnit()
 local integer role=0
 local integer target=0
 local integer oid=GetIssuedOrderId()
 if actor==cpArcher then
  set role=1
 elseif actor==cpHippo then
  set role=2
 else
  return
 endif
 if GetOrderTargetUnit()==cpArcher then
  set target=1
 elseif GetOrderTargetUnit()==cpHippo then
  set target=2
 endif
 call Preload("COUP tick="+I2S(cpTick)+" scene="+I2S(cpScene)+" label=issued-"+kind+" oid="+I2S(oid)+" actor="+I2S(role)+" target="+I2S(target)+" ah="+I2S(GetUnitCurrentOrder(cpArcher))+" hh="+I2S(GetUnitCurrentOrder(cpHippo)))
 if not cpNested and kind=="immediate" and oid==851972 and role==2 and ModuloInteger(cpTick,350)==115 then
  set cpNested=true
  call CpMark("nested-before",true)
  if cpScene==0 then
   call CpMark("nested-original-move-return",IssuePointOrder(cpArcher,"move",512.0,768.0))
  else
   call RemoveUnit(cpArcher)
   call CpMark("nested-original-remove-return",true)
  endif
 endif
 set actor=null
endfunction
function CpTargetEvent takes nothing returns nothing
 call CpEvent("target")
endfunction
function CpPointEvent takes nothing returns nothing
 call CpEvent("point")
endfunction
function CpImmediateEvent takes nothing returns nothing
 call CpEvent("immediate")
endfunction
function CpTick takes nothing returns nothing
 local integer age=ModuloInteger(cpTick,350)
 local real ax=704.0
 local group g=null
 local unit u=null

 if age==0 then
  set cpNested=false
  set cpArcher=CreateUnit(Player(0),'earc',ax,1024.0,180.0)
  set cpHippo=CreateUnit(Player(0),'ehip',1536.0,1024.0,0.0)
  call UnitAddAbility(cpArcher,'Acoa')
  call UnitAddAbility(cpHippo,'Acoh')
  call SetUnitAcquireRange(cpArcher,0.0)
  call SetUnitAcquireRange(cpHippo,0.0)
  call CpMark("spawn",true)
 elseif age==100 then
  call CpMark("before",true)
  call CpMark("order-return",IssueTargetOrder(cpArcher,"mounthippogryph",cpHippo))
 elseif age==115 then
  call CpMark("before-interrupt",true)
  call CpMark("stop-archer",IssueImmediateOrder(cpArcher,"stop"))
 elseif age==348 then
  call CpMark("settled",true)
 elseif age==349 then
  set g=CreateGroup()
  call GroupEnumUnitsOfPlayer(g,Player(0),null)
  loop
   set u=FirstOfGroup(g)
   exitwhen u==null
   call GroupRemoveUnit(g,u)
   call RemoveUnit(u)
  endloop
  call DestroyGroup(g)
  set cpArcher=null
  set cpHippo=null
  set cpScene=cpScene+1
  if cpScene==2 then
   call Preload("COUP tick="+I2S(cpTick)+" scene=2 label=complete")
   call PreloadGenEnd("PRELOAD_FILE")
   call PauseTimer(cpTimer)
   return
  endif
 elseif age>100 and ModuloInteger(age,5)==0 then
  call CpMark("sample",true)
 endif
 set cpTick=cpTick+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger et=CreateTrigger()
 local trigger ep=CreateTrigger()
 local trigger ei=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(et,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(ep,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(ei,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
 call TriggerAddAction(et,function CpTargetEvent)
 call TriggerAddAction(ep,function CpPointEvent)
 call TriggerAddAction(ei,function CpImmediateEvent)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerTechResearched(Player(0),'Reht',1)
 call PreloadGenClear()
 call PreloadGenStart()
 call Preload("COUP tick=0 scene=-1 label=start")
 call PreloadGenEnd("START_FILE")
 call PreloadGenClear()
 call PreloadGenStart()
 set cpTimer=CreateTimer()
 call TimerStart(cpTimer,0.02,true,function CpTick)
endfunction
