globals
 unit udg_O345Unit=null
 unit udg_O345Target=null
 unit udg_O345Alternative=null
 timer udg_O345Timer=null
 integer udg_O345Tick=0
 integer udg_O345Scene=0
endglobals
function O345Mark takes string label returns nothing
 local integer enemy=0
 if IsUnitEnemy(udg_O345Target,GetOwningPlayer(udg_O345Unit)) then
  set enemy=1
 endif
 call Preload("O345 tick="+I2S(udg_O345Tick)+" scene="+I2S(udg_O345Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_O345Unit))+" x="+R2S(GetUnitX(udg_O345Unit))+" y="+R2S(GetUnitY(udg_O345Unit))+" enemy="+I2S(enemy)+" targetlife="+R2S(GetWidgetLife(udg_O345Target))+" altlife="+R2S(GetWidgetLife(udg_O345Alternative)))
endfunction
function O345Issued takes nothing returns nothing
 local string target="none"
 if GetOrderTargetUnit()==udg_O345Target then
  set target="original"
 elseif GetOrderTargetUnit()==udg_O345Alternative then
  set target="alternative"
 endif
 call O345Mark("event_"+I2S(GetIssuedOrderId())+"_"+target)
endfunction
function O345Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_O345Tick,180)
 if phase==0 then
  if udg_O345Scene==0 then
   set udg_O345Unit=CreateUnit(Player(0),'hfoo',272,304,90)
   set udg_O345Target=CreateUnit(Player(1),'hpea',1008,1040,90)
  else
   set udg_O345Unit=CreateUnit(Player(1),'hfoo',272,304,90)
   set udg_O345Target=CreateUnit(Player(0),'hpea',1008,1040,90)
  endif
  call UnitRemoveAbility(udg_O345Target,'Amov')
  call UnitRemoveAbility(udg_O345Target,'Aatk')
  call SetUnitAcquireRange(udg_O345Unit,0)
 elseif phase==5 then
  call IssueTargetOrder(udg_O345Unit,"attack",udg_O345Target)
  call O345Mark("issued")
 elseif phase==20 and udg_O345Scene>=2 then
  set udg_O345Alternative=CreateUnit(Player(0),'hpea',700,1000,90)
  call UnitRemoveAbility(udg_O345Alternative,'Amov')
  call UnitRemoveAbility(udg_O345Alternative,'Aatk')
  if udg_O345Scene==3 then
   call SetUnitInvulnerable(udg_O345Alternative,true)
  endif
  call O345Mark("alternative")
 elseif phase==175 then
  call RemoveUnit(udg_O345Unit)
  call RemoveUnit(udg_O345Target)
  call RemoveUnit(udg_O345Alternative)
  set udg_O345Alternative=null
 endif
 call O345Mark("sample")
 set udg_O345Tick=udg_O345Tick+1
 if ModuloInteger(udg_O345Tick,180)==0 then
  set udg_O345Scene=udg_O345Scene+1
  if udg_O345Scene==4 then
   call O345Mark("complete")
   call PreloadGenEnd("order345.txt")
   call PauseTimer(udg_O345Timer)
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger orders=CreateTrigger()
 local integer x=24
 local integer y=25
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call TriggerRegisterPlayerUnitEvent(orders,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(orders,Player(1),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(orders,Player(1),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddAction(orders,function O345Issued)
 loop
  exitwhen y>39
  set x=24
  loop
   exitwhen x>38
   if x==24 or x==38 or y==25 or y==39 then
    call SetTerrainPathable(I2R(x*32+16),I2R(y*32+16),PATHING_TYPE_WALKABILITY,false)
   endif
   set x=x+1
  endloop
  set y=y+1
 endloop
 call O345Mark("start")
 set udg_O345Timer=CreateTimer()
 call TimerStart(udg_O345Timer,.1,true,function O345Tick)
endfunction
