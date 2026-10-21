globals
 timer cpTimer=null
 unit cpArcher=null
 unit cpHippo=null
 integer cpTick=0
 integer cpScene=0
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
function CpTick takes nothing returns nothing
 local integer age=ModuloInteger(cpTick,350)
 local integer interrupt=115
 local real ax=704.0
 local group g=null
 local unit u=null
 if cpScene>=9 then
  set interrupt=180
 endif
 if age==0 then
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
 elseif age==interrupt then
  call CpMark("before-interrupt",true)
  if cpScene==1 or cpScene==9 then
   call CpMark("stop-archer",IssueImmediateOrder(cpArcher,"stop"))
  elseif cpScene==2 or cpScene==10 then
   call CpMark("stop-hippo",IssueImmediateOrder(cpHippo,"stop"))
  elseif cpScene==3 then
   call CpMark("move-archer",IssuePointOrder(cpArcher,"move",512.0,768.0))
  elseif cpScene==4 then
   call CpMark("move-hippo",IssuePointOrder(cpHippo,"move",1792.0,768.0))
  elseif cpScene==5 then
   call RemoveUnit(cpArcher)
   set cpArcher=null
   call CpMark("remove-archer",true)
  elseif cpScene==6 then
   call RemoveUnit(cpHippo)
   set cpHippo=null
   call CpMark("remove-hippo",true)
  elseif cpScene==7 then
   call PauseUnit(cpArcher,true)
   call CpMark("pause-archer",true)
  elseif cpScene==8 then
   call PauseUnit(cpHippo,true)
   call CpMark("pause-hippo",true)
  else
   call CpMark("control",true)
  endif
 elseif age==165 and (cpScene==7 or cpScene==8) then
  call CpMark("before-resume",true)
  if cpScene==7 then
   call PauseUnit(cpArcher,false)
  else
   call PauseUnit(cpHippo,false)
  endif
  call CpMark("resume",true)
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
  if cpScene==11 then
   call Preload("COUP tick="+I2S(cpTick)+" scene=11 label=complete")
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
