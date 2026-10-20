globals
 timer timer022=null
 integer tick022=0
 integer scene022=-1
 integer age022=0
 integer length022=0
 unit c022=null
 unit f022=null
 real cx022=0.0
 real cy022=0.0
 real fx022=0.0
 real fy022=0.0
 string variant022="@VARIANT@"
endglobals
function Order022 takes unit u returns integer
 if u==null then
  return -1
 endif
 return GetUnitCurrentOrder(u)
endfunction
function Moved022 takes unit u,real x,real y returns integer
 if u==null then
  return -1
 endif
 if (GetUnitX(u)-x)*(GetUnitX(u)-x)+(GetUnitY(u)-y)*(GetUnitY(u)-y)>256.0 then
  return 1
 endif
 return 0
endfunction
function Owner022 takes unit u returns integer
 if u==null then
  return -1
 endif
 return GetPlayerId(GetOwningPlayer(u))
endfunction
function Prefix022 takes string label returns string
 return "O022 tick="+I2S(tick022)+" scene="+I2S(scene022)+" age="+I2S(age022)+" label="+label
endfunction
function Mark022 takes string label returns nothing
 call Preload(Prefix022(label)+" c="+I2S(Order022(c022))+" f="+I2S(Order022(f022))+" cmoved="+I2S(Moved022(c022,cx022,cy022))+" fmoved="+I2S(Moved022(f022,fx022,fy022))+" cowner="+I2S(Owner022(c022)))
endfunction
function Issued022 takes string kind returns nothing
 call Preload(Prefix022("issued")+" kind="+kind+" unit="+I2S(GetUnitUserData(GetTriggerUnit()))+" event="+I2S(GetIssuedOrderId())+" head="+I2S(GetUnitCurrentOrder(GetTriggerUnit()))+" player="+I2S(GetPlayerId(GetTriggerPlayer()))+" owner="+I2S(Owner022(GetTriggerUnit())))
endfunction
function IssuedImmediate022 takes nothing returns boolean
 call Issued022("immediate")
 return true
endfunction
function IssuedPoint022 takes nothing returns boolean
 call Issued022("point")
 return true
endfunction
function IssuedTarget022 takes nothing returns boolean
 call Issued022("target")
 return true
endfunction
function Spell022 takes string phase returns nothing
 call Preload(Prefix022("spell")+" phase="+phase+" unit="+I2S(GetUnitUserData(GetTriggerUnit()))+" ability="+I2S(GetSpellAbilityId())+" head="+I2S(GetUnitCurrentOrder(GetTriggerUnit())))
endfunction
function SpellChannel022 takes nothing returns boolean
 call Spell022("channel")
 return true
endfunction
function SpellCast022 takes nothing returns boolean
 call Spell022("cast")
 return true
endfunction
function SpellEffect022 takes nothing returns boolean
 call Spell022("effect")
 return true
endfunction
function SpellFinish022 takes nothing returns boolean
 call Spell022("finish")
 return true
endfunction
function SpellEnd022 takes nothing returns boolean
 call Spell022("endcast")
 return true
endfunction
function OwnerEvent022 takes nothing returns boolean
 call Preload(Prefix022("owner-event")+" unit="+I2S(GetUnitUserData(GetChangingUnit()))+" previous="+I2S(GetPlayerId(GetChangingUnitPrevOwner()))+" owner="+I2S(Owner022(GetChangingUnit()))+" player="+I2S(GetPlayerId(GetTriggerPlayer()))+" head="+I2S(GetUnitCurrentOrder(GetChangingUnit())))
 return true
endfunction
function Register022 takes player p returns nothing
 local trigger t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,p,EVENT_PLAYER_UNIT_ISSUED_ORDER,null)
 call TriggerAddCondition(t,Condition(function IssuedImmediate022))
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,p,EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddCondition(t,Condition(function IssuedPoint022))
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,p,EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerAddCondition(t,Condition(function IssuedTarget022))
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,p,EVENT_PLAYER_UNIT_SPELL_CHANNEL,null)
 call TriggerAddCondition(t,Condition(function SpellChannel022))
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,p,EVENT_PLAYER_UNIT_SPELL_CAST,null)
 call TriggerAddCondition(t,Condition(function SpellCast022))
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,p,EVENT_PLAYER_UNIT_SPELL_EFFECT,null)
 call TriggerAddCondition(t,Condition(function SpellEffect022))
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,p,EVENT_PLAYER_UNIT_SPELL_FINISH,null)
 call TriggerAddCondition(t,Condition(function SpellFinish022))
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,p,EVENT_PLAYER_UNIT_SPELL_ENDCAST,null)
 call TriggerAddCondition(t,Condition(function SpellEnd022))
 set t=CreateTrigger()
 call TriggerRegisterPlayerUnitEvent(t,p,EVENT_PLAYER_UNIT_CHANGE_OWNER,null)
 call TriggerAddCondition(t,Condition(function OwnerEvent022))
endfunction
function Name022 takes nothing returns string
 if variant022=="ui" then
  if scene022==0 then
   return "windup-single-move"
  elseif scene022==1 then
   return "windup-single-stop"
  elseif scene022==2 then
   return "windup-single-shift"
  elseif scene022==3 then
   return "windup-mixed-move"
  elseif scene022==4 then
   return "work-single-move"
  elseif scene022==5 then
   return "work-mixed-move"
  endif
  return "work-mixed-hold"
 endif
 if scene022==0 then
  return "owner-idle"
 elseif scene022==1 then
  return "owner-walk"
 elseif scene022==2 then
  return "owner-attack"
 elseif scene022==3 then
  return "owner-queued"
 elseif scene022==4 then
  return "repair-reissue"
 elseif scene022==5 then
  return "repair-smart"
 endif
 return "repair-none"
endfunction
function Begin022 takes nothing returns nothing
 local integer mixed=0
 set age022=0
 if variant022=="ui" then
  set length022=220
  set c022=CreateUnit(Player(3),'hfoo',512.0,600.0,0.0)
  set f022=CreateUnit(Player(3),'hfoo',512.0,420.0,0.0)
  call UnitAddAbility(c022,'AQ22')
  if scene022==3 or scene022==5 or scene022==6 then
   set mixed=1
  endif
 elseif scene022<4 then
  set length022=120
  set c022=CreateUnit(Player(3),'hfoo',512.0,512.0,0.0)
  if scene022==2 then
   set f022=CreateUnit(Player(PLAYER_NEUTRAL_AGGRESSIVE),'hbar',768.0,512.0,0.0)
  endif
  if scene022==3 then
   call SetUnitMoveSpeed(c022,100.0)
  endif
 else
  set length022=450
  set c022=CreateUnit(Player(3),'hpea',448.0,512.0,0.0)
  set f022=CreateUnit(Player(3),'hbar',704.0,512.0,0.0)
  call SetWidgetLife(f022,GetUnitState(f022,UNIT_STATE_MAX_LIFE)*0.75)
 endif
 call SetUnitUserData(c022,1)
 call SetUnitAcquireRange(c022,0.0)
 set cx022=GetUnitX(c022)
 set cy022=GetUnitY(c022)
 if f022!=null then
  call SetUnitUserData(f022,2)
  call SetUnitAcquireRange(f022,0.0)
  set fx022=GetUnitX(f022)
  set fy022=GetUnitY(f022)
 endif
 call ClearSelection()
 call SelectUnit(c022,true)
 if mixed==1 then
  call SelectUnit(f022,true)
 endif
 call Mark022("created name="+Name022()+" mixed="+I2S(mixed))
endfunction
function Action022 takes nothing returns nothing
 if variant022=="ui" then
  if age022==10 then
   call Mark022("cast accepted="+I2S(IntegerTertiaryOp(IssueImmediateOrder(c022,"channel"),1,0)))
  endif
  return
 endif
 if scene022==1 and age022==10 then
  call Mark022("walk accepted="+I2S(IntegerTertiaryOp(IssuePointOrder(c022,"move",1800.0,512.0),1,0)))
 elseif scene022==2 and age022==5 then
  call Mark022("attack accepted="+I2S(IntegerTertiaryOp(IssueTargetOrder(c022,"attack",f022),1,0)))
 elseif scene022>=4 and age022==5 then
  call Mark022("repair accepted="+I2S(IntegerTertiaryOp(IssueTargetOrder(c022,"repair",f022),1,0)))
 elseif scene022==4 and age022==100 then
  call Mark022("reissue-repair accepted="+I2S(IntegerTertiaryOp(IssueTargetOrder(c022,"repair",f022),1,0)))
 elseif scene022==5 and age022==100 then
  call Mark022("reissue-smart accepted="+I2S(IntegerTertiaryOp(IssueTargetOrder(c022,"smart",f022),1,0)))
 endif
 if scene022<4 and ((scene022<2 and age022==30) or (scene022==2 and age022==50) or (scene022==3 and age022==70)) then
  call Mark022("owner-before")
  call SetUnitOwner(c022,Player(1),false)
  call Mark022("owner-after")
 endif
endfunction
function Tick022 takes nothing returns nothing
 set tick022=tick022+1
 if tick022==1 then
  call PreloadGenEnd("order022-"+variant022+"-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
  call Preload(Prefix022("ticking"))
 endif
 // Input pixels are calibrated for this fixed camera; no hotkey may move it.
 call SetCameraPosition(1024.0,512.0)
 if tick022<20 then
  return
 endif
 if scene022<0 or age022>=length022 then
  set scene022=scene022+1
  if scene022==7 then
   call Preload(Prefix022("complete")+" variant="+variant022)
   call PreloadGenEnd("order022-"+variant022+".txt")
   call PauseTimer(timer022)
   return
  endif
  call Begin022()
 endif
 call Action022()
 if age022/10*10==age022 then
  call Mark022("sample")
 endif
 if age022==length022-20 then
  call Mark022("terminal")
  call RemoveUnit(c022)
  if f022!=null then
   call RemoveUnit(f022)
  endif
  set c022=null
  set f022=null
 endif
 set age022=age022+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetPlayerState(Player(3),PLAYER_STATE_RESOURCE_GOLD,10000)
 call SetPlayerState(Player(3),PLAYER_STATE_RESOURCE_LUMBER,10000)
 call FogEnable(false)
 call FogMaskEnable(false)
 call Register022(Player(3))
 call Register022(Player(1))
 call SetCameraBounds(-2048.0,-2048.0,-2048.0,4096.0,4096.0,4096.0,4096.0,-2048.0)
 call ResetToGameCamera(0.0)
 call SetCameraPosition(1024.0,512.0)
 call Preload("O022 tick=0 label=start variant="+variant022+" player="+I2S(GetPlayerId(GetLocalPlayer())))
 set timer022=CreateTimer()
 call TimerStart(timer022,0.1,true,function Tick022)
endfunction
