globals
 timer timer022=null
 integer tick022=0
 integer scene022=-1
 integer age022=0
 integer length022=80
 unit c022=null
 unit f022=null
endglobals
function Mark022 takes string label returns nothing
 local integer head=-1
 if c022!=null then
  set head=GetUnitCurrentOrder(c022)
 endif
 call Preload("O022S tick="+I2S(tick022)+" scene="+I2S(scene022)+" age="+I2S(age022)+" label="+label+" head="+I2S(head))
endfunction
function Issued022 takes nothing returns boolean
 if GetUnitUserData(GetTriggerUnit())==1 then
  call Preload("O022S tick="+I2S(tick022)+" scene="+I2S(scene022)+" age="+I2S(age022)+" label=issued event="+I2S(GetIssuedOrderId())+" head="+I2S(GetUnitCurrentOrder(GetTriggerUnit()))+" target="+I2S(GetUnitUserData(GetOrderTargetUnit())))
 endif
 return true
endfunction
function Begin022 takes nothing returns nothing
 set age022=0
 set length022=80
 if scene022==3 then
  set length022=180
 endif
 set c022=CreateUnit(Player(3),'hfoo',512.0,600.0,0.0)
 set f022=CreateUnit(Player(1),'hfoo',1277.12,511.37,180.0)
 call SetUnitUserData(c022,1)
 call SetUnitUserData(f022,2)
 call SetUnitAcquireRange(c022,0.0)
 call PauseUnit(f022,true)
 call SetUnitScale(f022,2.0,2.0,2.0)
 if scene022>=3 then
  call UnitAddAbility(c022,'AQ22')
 endif
 call ClearSelection()
 call SelectUnit(c022,true)
 call Mark022("created")
endfunction
function Tick022 takes nothing returns nothing
 set tick022=tick022+1
 call SetCameraPosition(1024.0,512.0)
 if tick022==1 then
  call PreloadGenEnd("order022-smart-head-start.txt")
  call PreloadGenClear()
  call PreloadGenStart()
  call Mark022("ticking")
 endif
 if tick022<20 then
  return
 endif
 if scene022<0 or age022>=length022 then
  set scene022=scene022+1
  if scene022==5 then
   call Mark022("complete")
   call PreloadGenEnd("order022-smart-head.txt")
   call PauseTimer(timer022)
   return
  endif
  call Begin022()
 endif
 if age022==10 then
  if scene022==0 then
   call Mark022("jass-smart accepted="+I2S(IntegerTertiaryOp(IssueTargetOrder(c022,"smart",f022),1,0)))
  elseif scene022==1 then
   call Mark022("jass-attack accepted="+I2S(IntegerTertiaryOp(IssueTargetOrder(c022,"attack",f022),1,0)))
  elseif scene022>=3 then
   call Mark022("cast accepted="+I2S(IntegerTertiaryOp(IssueImmediateOrder(c022,"channel"),1,0)))
  endif
 endif
 if scene022==4 and age022==20 then
  call Mark022("jass-smart-cast accepted="+I2S(IntegerTertiaryOp(IssueTargetOrder(c022,"smart",f022),1,0)))
 endif
 if age022/10*10==age022 then
  call Mark022("sample")
 endif
 if age022==length022-1 then
  call Mark022("terminal")
  call RemoveUnit(c022)
  call RemoveUnit(f022)
  set c022=null
  set f022=null
 endif
 set age022=age022+1
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger t=CreateTrigger()
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(3),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(3),ALLIANCE_PASSIVE,false)
 call TriggerRegisterPlayerUnitEvent(t,Player(3),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerAddCondition(t,Condition(function Issued022))
 call SetCameraBounds(-2048.0,-2048.0,-2048.0,4096.0,4096.0,4096.0,4096.0,-2048.0)
 call ResetToGameCamera(0.0)
 call SetCameraPosition(1024.0,512.0)
 call Preload("O022S tick=0 label=start")
 set timer022=CreateTimer()
 call TimerStart(timer022,0.1,true,function Tick022)
endfunction
