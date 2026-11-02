globals
 timer udg_M348Timer=null
 integer udg_M348Tick=0
 unit array udg_M348Units
 group udg_M348Group=null
endglobals
function M348Mark takes string label returns nothing
 local integer i=0
 call Preload("M348 tick="+I2S(udg_M348Tick)+" label="+label)
 loop
  exitwhen i==8
  call Preload("M348 tick="+I2S(udg_M348Tick)+" label=unit u="+I2S(i)+" order="+I2S(GetUnitCurrentOrder(udg_M348Units[i]))+" x="+R2SW(GetUnitX(udg_M348Units[i]),1,4)+" y="+R2SW(GetUnitY(udg_M348Units[i]),1,4)+" facing="+R2SW(GetUnitFacing(udg_M348Units[i]),1,4)+" speed="+R2SW(GetUnitMoveSpeed(udg_M348Units[i]),1,4)+" turn="+R2SW(GetUnitTurnSpeed(udg_M348Units[i]),1,4)+" window="+R2SW(GetUnitPropWindow(udg_M348Units[i]),1,4))
  set i=i+1
 endloop
 call PreloadGenEnd("move348.txt")
endfunction
function M348Tick takes nothing returns nothing
 local integer i=0
 set udg_M348Tick=udg_M348Tick+1
 if udg_M348Tick==1 then
  call M348Mark("start")
 elseif udg_M348Tick==10 then
  loop
   exitwhen i==6
   set udg_M348Units[i]=CreateUnit(Player(0),'hM48',512,256+256*I2R(i),0)
   set i=i+1
  endloop
  set udg_M348Units[6]=CreateUnit(Player(0),'hM48',1700,512,0)
  set udg_M348Units[7]=CreateUnit(Player(0),'hM48',1700,1792,0)
  set udg_M348Group=CreateGroup()
  call GroupAddUnit(udg_M348Group,udg_M348Units[2])
  call GroupAddUnit(udg_M348Group,udg_M348Units[3])
  call SetUnitMoveSpeed(udg_M348Units[3],211)
  call M348Mark("created")
 elseif udg_M348Tick==12 then
  call IssuePointOrder(udg_M348Units[0],"move",1536,256)
  call IssueTargetOrder(udg_M348Units[1],"smart",udg_M348Units[6])
  call GroupPointOrder(udg_M348Group,"move",1536,1024)
  call IssuePointOrder(udg_M348Units[5],"move",1536,1536)
  call M348Mark("initial-requests")
 elseif udg_M348Tick==14 then
  call SetUnitTurnSpeed(udg_M348Units[0],0.125)
  call SetUnitPropWindow(udg_M348Units[0],0.1)
  call SetUnitMoveSpeed(udg_M348Units[1],211)
  call SetUnitFacingTimed(udg_M348Units[4],180,2.0)
  call SetUnitFacingTimed(udg_M348Units[5],180,2.0)
  call M348Mark("setters-then-facing")
 elseif udg_M348Tick==16 then
  call SetUnitTurnSpeed(udg_M348Units[4],0.5)
  call SetUnitTurnSpeed(udg_M348Units[5],0.7)
  call SetUnitPropWindow(udg_M348Units[4],0.05)
  call SetUnitPropWindow(udg_M348Units[5],0.8)
  call SetUnitMoveSpeed(udg_M348Units[2],173)
  call SetUnitMoveSpeed(udg_M348Units[3],389)
  call M348Mark("setters-during-overrides")
 elseif udg_M348Tick==18 then
  call IssuePointOrder(udg_M348Units[4],"move",1536,1280)
  call IssueTargetOrder(udg_M348Units[5],"smart",udg_M348Units[7])
  call M348Mark("replace-facing")
 elseif udg_M348Tick==20 then
  call SetUnitMoveSpeed(udg_M348Units[0],173)
  call SetUnitMoveSpeed(udg_M348Units[1],389)
  call M348Mark("speed-overwrites")
 elseif udg_M348Tick==22 then
  call IssueTargetOrder(udg_M348Units[0],"move",udg_M348Units[6])
  call IssuePointOrder(udg_M348Units[1],"move",1536,768)
  call M348Mark("point-target-swap")
 elseif udg_M348Tick==24 then
  call GroupImmediateOrder(udg_M348Group,"stop")
  call GroupPointOrder(udg_M348Group,"move",1536,1536)
  call M348Mark("new-group")
 elseif udg_M348Tick==26 then
  call SetUnitFacing(udg_M348Units[0],315)
  call M348Mark("immediate-facing")
 elseif udg_M348Tick==28 then
  call SetUnitFacingTimed(udg_M348Units[1],270,1.2)
  call IssuePointOrder(udg_M348Units[0],"move",1536,256)
  call M348Mark("facing-replaces-point")
 elseif udg_M348Tick==30 then
  call SetUnitPropWindow(udg_M348Units[0],6.2832)
  call SetUnitTurnSpeed(udg_M348Units[0],6.4)
  call M348Mark("normalized-setters")
 elseif udg_M348Tick==32 then
  call SetUnitMoveSpeed(udg_M348Units[1],173)
  call M348Mark("speed-during-facing")
 elseif udg_M348Tick==34 then
  call IssuePointOrder(udg_M348Units[1],"move",1536,512)
  call M348Mark("point-after-facing")
 elseif udg_M348Tick==36 then
  call UnitRemoveAbility(udg_M348Units[2],'Amov')
  call SetUnitMoveSpeed(udg_M348Units[2],389)
  call M348Mark("disabled-setter")
 elseif udg_M348Tick==38 then
  call UnitAddAbility(udg_M348Units[2],'Amov')
  call GroupPointOrder(udg_M348Group,"move",1536,1024)
  call M348Mark("readmit")
 elseif udg_M348Tick==44 then
  call SetUnitMoveSpeed(udg_M348Units[2],333)
  call SetUnitMoveSpeed(udg_M348Units[3],333)
  call GroupPointOrder(udg_M348Group,"move",1536,1536)
  call M348Mark("reset-group-speeds")
 elseif udg_M348Tick==60 then
  loop
   exitwhen i==8
   call IssueImmediateOrder(udg_M348Units[i],"stop")
   set i=i+1
  endloop
  call M348Mark("stop")
 elseif udg_M348Tick==70 then
  call M348Mark("complete")
  call PauseTimer(udg_M348Timer)
 elseif udg_M348Tick>12 then
  call M348Mark("sample")
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call PreloadGenClear()
 call PreloadGenStart()
 set udg_M348Timer=CreateTimer()
 call TimerStart(udg_M348Timer,0.1,true,function M348Tick)
endfunction
