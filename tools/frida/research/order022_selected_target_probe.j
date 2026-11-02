globals
 timer timer022=null
 integer tick022=0
 integer scene022=-1
 integer age022=0
 unit c022=null
 unit p022=null
 unit f022=null
 unit n022=null
 boolean changed022=false
endglobals
function Mark022 takes string label returns nothing
 local integer c=0
 local integer p=0
 if c022!=null then
  set c=GetUnitCurrentOrder(c022)
 endif
 if p022!=null then
  set p=GetUnitCurrentOrder(p022)
 endif
 call Preload("O022T tick="+I2S(tick022)+" scene="+I2S(scene022)+" age="+I2S(age022)+" label="+label+" c="+I2S(c)+" p="+I2S(p))
endfunction
function Changed022 takes unit u returns nothing
 if GetUnitUserData(u)==1 and not changed022 then
  set changed022=true
  call RemoveUnit(f022)
  set n022=CreateUnit(Player(1),'hfoo',900.0,900.0,0.0)
  call SetUnitUserData(n022,4)
  call SetUnitAcquireRange(n022,0.0)
  call PauseUnit(n022,true)
  call Mark022("target-replaced")
 endif
endfunction
function IssuedTarget022 takes nothing returns boolean
 call Mark022("issued-target unit="+I2S(GetUnitUserData(GetTriggerUnit()))+" id="+I2S(GetIssuedOrderId())+" target="+I2S(GetUnitUserData(GetOrderTargetUnit())))
 call Changed022(GetTriggerUnit())
 return true
endfunction
function IssuedPoint022 takes nothing returns boolean
 call Mark022("issued-point unit="+I2S(GetUnitUserData(GetTriggerUnit()))+" id="+I2S(GetIssuedOrderId())+" x="+R2S(GetOrderPointX())+" y="+R2S(GetOrderPointY()))
 call Changed022(GetTriggerUnit())
 return true
endfunction
function Begin022 takes nothing returns nothing
 set age022=0
 set changed022=false
 set c022=CreateUnit(Player(3),'hfoo',512.0,600.0,0.0)
 set p022=CreateUnit(Player(3),'hfoo',512.0,420.0,0.0)
 set f022=CreateUnit(Player(1),'hfoo',1277.12,511.37,180.0)
 call SetUnitUserData(c022,1)
 call SetUnitUserData(p022,3)
 call SetUnitUserData(f022,2)
 call SetUnitAcquireRange(c022,0.0)
 call SetUnitAcquireRange(p022,0.0)
 call PauseUnit(f022,true)
 call SetUnitScale(f022,2.0,2.0,2.0)
 call ClearSelection()
 call SelectUnit(c022,true)
 call SelectUnit(p022,true)
 call Mark022("created")
endfunction
function Tick022 takes nothing returns nothing
 set tick022=tick022+1
 call SetCameraPosition(1024.0,512.0)
 if tick022==1 then
  call PreloadGenEnd("order022-selected-target-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
  call Mark022("ticking")
 endif
 if tick022<20 then
  return
 endif
 if scene022<0 or age022>=80 then
  set scene022=scene022+1
  if scene022==2 then
   call Mark022("complete")
   call PreloadGenEnd("order022-selected-target.txt")
   call PauseTimer(timer022)
   return
  endif
  call Begin022()
 endif
 if age022/10*10==age022 then
  call Mark022("sample cx="+R2S(GetUnitX(c022))+" cy="+R2S(GetUnitY(c022))+" px="+R2S(GetUnitX(p022))+" py="+R2S(GetUnitY(p022)))
 endif
 if age022==79 then
  call Mark022("terminal cx="+R2S(GetUnitX(c022))+" cy="+R2S(GetUnitY(c022))+" px="+R2S(GetUnitX(p022))+" py="+R2S(GetUnitY(p022)))
  call RemoveUnit(c022)
  call RemoveUnit(p022)
  call RemoveUnit(n022)
  set c022=null
  set p022=null
  set f022=null
  set n022=null
 endif
 set age022=age022+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=CreateTrigger()
 local trigger p=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(3),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(3),ALLIANCE_PASSIVE,false)
 call TriggerRegisterPlayerUnitEvent(t,Player(3),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerAddCondition(t,Condition(function IssuedTarget022))
 call TriggerRegisterPlayerUnitEvent(p,Player(3),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddCondition(p,Condition(function IssuedPoint022))
 call SetCameraBounds(-2048.0,-2048.0,-2048.0,4096.0,4096.0,4096.0,4096.0,-2048.0)
 call ResetToGameCamera(0.0)
 call SetCameraPosition(1024.0,512.0)
 call Preload("O022T tick=0 label=start")
 set timer022=CreateTimer()
 call TimerStart(timer022,0.1,true,function Tick022)
endfunction
