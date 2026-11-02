globals
 timer udg_C320Timer=null
 integer udg_C320Tick=0
 integer udg_C320Scene=0
 unit array udg_C320Units
 unit udg_C320Caster=null
 boolean udg_C320Held=false
endglobals
function C320Mark takes string label returns nothing
 local integer i=0
 call Preload("C320 tick="+I2S(udg_C320Tick)+" scene="+I2S(udg_C320Scene)+" label="+label)
 loop
  exitwhen i==3
  call Preload("C320 tick="+I2S(udg_C320Tick)+" scene="+I2S(udg_C320Scene)+" label=unit u="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_C320Units[i]))+" x="+R2SW(GetUnitX(udg_C320Units[i]),1,4)+" y="+R2SW(GetUnitY(udg_C320Units[i]),1,4)+" speed="+R2SW(GetUnitMoveSpeed(udg_C320Units[i]),1,4))
  set i=i+1
 endloop
 call PreloadGenEnd("captain320-test.txt")
endfunction
function C320Tick takes nothing returns nothing
 local integer i=0
 local unit source=null
 set udg_C320Tick=udg_C320Tick+1
 if udg_C320Tick==1 then
  call C320Mark("start")
  call PreloadGenEnd("captain320-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif udg_C320Tick==20 then
  call C320Mark("begin-setup")
  loop
   exitwhen i==3
   set udg_C320Units[i]=CreateUnit(Player(0),'hTAA',512+I2R(i)*96,512,0)
   call SetUnitAcquireRange(udg_C320Units[i],0)
   set i=i+1
  endloop
  set udg_C320Caster=CreateUnit(Player(1),'hsor',480,448,0)
  call UnitRemoveAbility(udg_C320Caster,'Aatk')
  if IssueImmediateOrder(udg_C320Caster,"slowon") then
   call Preload("C321 rejected")
  else
   call Preload("C321 autocast on")
  endif
  if IssueImmediateOrder(udg_C320Caster,"slowoff") then
   call Preload("C321 autocast off")
  else
   call Preload("C321 rejected")
  endif
  call C320Mark("created")
 elseif udg_C320Tick==22 then
  call StartCampaignAI(Player(0),"Scripts\\captain320.ai")
  call C320Mark("start-ai")
 elseif udg_C320Tick==35 then
  if IssueTargetOrder(udg_C320Caster,"slow",udg_C320Units[0]) then
   call Preload("C321 accepted")
  else
   call Preload("C321 rejected")
  endif
  call C320Mark("slow-request")
 elseif udg_C320Tick==60 then
  call UnitRemoveBuffs(udg_C320Units[0],false,true)
  call C320Mark("remove-slow")
 elseif udg_C320Tick==90 then
  loop
   exitwhen i==3
   call IssueImmediateOrder(udg_C320Units[i],"stop")
   set i=i+1
  endloop
  call C320Mark("stop")
 elseif udg_C320Tick==110 then
  call C320Mark("complete")
  call PauseTimer(udg_C320Timer)
 elseif udg_C320Tick>22 then
  call C320Mark("sample")
 endif
 if not udg_C320Held and GetUnitAbilityLevel(udg_C320Units[0],'Bslo')>0 then
  call PauseUnit(udg_C320Caster,true)
  set udg_C320Held=true
  call C320Mark("slow-applied")
 endif
endfunction
function C321Phase0 takes nothing returns nothing
 call Preload("C321 phase 0")
endfunction
function C321Phase1 takes nothing returns nothing
 call Preload("C321 phase 1")
endfunction
function C321Phase2 takes nothing returns nothing
 call Preload("C321 phase 2")
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=null
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,Player(1),ConvertPlayerUnitEvent(272),null)
 call TriggerAddAction(t,function C321Phase0)
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,Player(1),ConvertPlayerUnitEvent(273),null)
 call TriggerAddAction(t,function C321Phase1)
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,Player(1),ConvertPlayerUnitEvent(274),null)
 call TriggerAddAction(t,function C321Phase2)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_C320Timer=CreateTimer()
 call TimerStart(udg_C320Timer,0.1,true,function C320Tick)
endfunction

function main takes nothing returns nothing
 call PathProbeInit()
endfunction
