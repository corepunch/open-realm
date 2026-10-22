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
function CpEmit takes string value returns nothing
 if StringLength(value)>160 then
  call Preload("CPI tick="+I2S(cpTick)+" scene="+I2S(cpScene)+" label=length-error length="+I2S(StringLength(value)))
  call PreloadGenEnd("PRELOAD_FILE")
  call PauseTimer(cpTimer)
  return
 endif
 call Preload(value)
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
 call CpEmit("CPI tick="+I2S(cpTick)+" scene="+I2S(cpScene)+" label="+label+"-a ah="+I2S(GetUnitCurrentOrder(cpArcher))+" ax="+R2S(GetUnitX(cpArcher))+" ay="+R2S(GetUnitY(cpArcher))+" af="+R2S(GetUnitFacing(cpArcher))+" al="+R2S(GetWidgetLife(cpArcher)))
 call CpEmit("CPI tick="+I2S(cpTick)+" scene="+I2S(cpScene)+" label="+label+"-h hh="+I2S(GetUnitCurrentOrder(cpHippo))+" hx="+R2S(GetUnitX(cpHippo))+" hy="+R2S(GetUnitY(cpHippo))+" hf="+R2S(GetUnitFacing(cpHippo))+" hl="+R2S(GetWidgetLife(cpHippo)))
 call CpEmit("CPI tick="+I2S(cpTick)+" scene="+I2S(cpScene)+" label="+label+"-setup accepted="+I2S(CpBool(accepted))+" reht="+I2S(GetPlayerTechCount(Player(0),'Reht',true))+" at="+I2S(GetUnitTypeId(cpArcher))+" aa="+I2S(GetUnitAbilityLevel(cpArcher,'Aco2'))+" ht="+I2S(GetUnitTypeId(cpHippo))+" ha="+I2S(GetUnitAbilityLevel(cpHippo,'Aco3')))
 call CpEmit("CPI tick="+I2S(cpTick)+" scene="+I2S(cpScene)+" label="+label+"-result riders="+I2S(riders)+" rx="+R2S(rx)+" ry="+R2S(ry))

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
 call CpEmit("CPI tick="+I2S(cpTick)+" scene="+I2S(cpScene)+" label=issued-"+kind+" oid="+I2S(oid)+" actor="+I2S(role)+" target="+I2S(target)+" ah="+I2S(GetUnitCurrentOrder(cpArcher))+" hh="+I2S(GetUnitCurrentOrder(cpHippo)))
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
 local integer age=ModuloInteger(cpTick,400)
 local real ax=704.0
 local group g=null
 local unit u=null
 if cpScene==0 or cpScene==2 then
  set ax=1280.0
 endif

 if age==0 then
  set cpNested=false
  set cpArcher=CreateUnit(Player(0),'earc',ax,1024.0,180.0)
  set cpHippo=CreateUnit(Player(0),'ehip',1536.0,1024.0,0.0)
  call SetUnitAcquireRange(cpArcher,0.0)
  call SetUnitAcquireRange(cpHippo,0.0)
  call CpEmit("CPI tick="+I2S(cpTick)+" scene="+I2S(cpScene)+" label=stock-data arange=99999 hrange=0 area=900 ab=0 hb=1 targs=vuln,invu")
  call CpEmit("CPI tick="+I2S(cpTick)+" scene="+I2S(cpScene)+" label=spawn-speed as="+R2S(GetUnitMoveSpeed(cpArcher))+" hs="+R2S(GetUnitMoveSpeed(cpHippo)))
  call CpMark("spawn",true)
 elseif age==100 then
  call CpMark("before",true)
  if cpScene<2 then
   call CpMark("immediate-archer-return",IssueImmediateOrder(cpArcher,"coupleinstant"))
  elseif cpScene<4 then
   call CpMark("immediate-hippo-return",IssueImmediateOrder(cpHippo,"coupleinstant"))
  elseif cpScene==4 then
   call CpMark("target-archer-return",IssueTargetOrder(cpArcher,"coupletarget",cpHippo))
  else
   call CpMark("target-hippo-return",IssueTargetOrder(cpHippo,"coupletarget",cpArcher))
  endif
 elseif age==398 then
  call CpMark("settled",true)
 elseif age==399 then
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
  if cpScene==6 then
   call CpEmit("CPI tick="+I2S(cpTick)+" scene=6 label=complete")
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
 call CpEmit("CPI tick=0 scene=-1 label=start")
 call PreloadGenEnd("START_FILE")
 call PreloadGenClear()
 call PreloadGenStart()
 set cpTimer=CreateTimer()
 call TimerStart(cpTimer,0.02,true,function CpTick)
endfunction
