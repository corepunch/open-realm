globals
 unit udg_O346Unit=null
 unit udg_O346Carrier=null
 unit udg_O346Alternate=null
 boolean udg_O346Removed=false
 timer udg_O346Timer=null
 integer udg_O346Tick=0
 integer udg_O346Scene=0
endglobals
function O346Mark takes string label returns nothing
 local integer loaded=0
 if IsUnitLoaded(udg_O346Unit) then
  set loaded=1
 endif
 call Preload("O346 tick="+I2S(udg_O346Tick)+" scene="+I2S(udg_O346Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_O346Unit))+" x="+R2S(GetUnitX(udg_O346Unit))+" y="+R2S(GetUnitY(udg_O346Unit))+" loaded="+I2S(loaded)+" alternateorder="+I2S(GetUnitCurrentOrder(udg_O346Alternate))+" carrierorder="+I2S(GetUnitCurrentOrder(udg_O346Carrier))+" cx="+R2S(GetUnitX(udg_O346Carrier))+" cy="+R2S(GetUnitY(udg_O346Carrier)))
endfunction
function O346Issued takes nothing returns nothing
 if GetTriggerUnit()==udg_O346Unit or GetTriggerUnit()==udg_O346Carrier then
  call O346Mark("event_"+I2S(GetIssuedOrderId()))
 endif
endfunction
function O346BlockWater takes nothing returns nothing
 local integer x=22
 local integer y=0
 loop
  exitwhen y>=64
  set x=22
  loop
   exitwhen x>27
   call SetTerrainPathable(I2R(x*32+16),I2R(y*32+16),PATHING_TYPE_FLOATABILITY,false)
   set x=x+1
  endloop
  set y=y+1
 endloop
 call O346Mark("water_blocked")
endfunction
function O346Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_O346Tick,600)
 local integer owner=udg_O346Scene/2
 local real destination=1600
 if ModuloInteger(udg_O346Scene,2)==1 then
  set destination=1600
 endif
 if phase==0 then
  set udg_O346Unit=CreateUnit(Player(owner),'hfoo',432,400,90)
  set udg_O346Carrier=CreateUnit(Player(owner),'hbot',400,400,90)
  call SetUnitAcquireRange(udg_O346Unit,0)
 elseif phase==5 then
  call SetUnitX(udg_O346Unit,destination)
  call SetUnitY(udg_O346Unit,400)
 elseif phase==20 then
  if IssueTargetOrder(udg_O346Unit,"board",udg_O346Carrier) then
   call O346Mark("board_accepted")
  else
   call O346Mark("board_rejected")
  endif
 elseif phase==25 then
  call O346BlockWater()
 elseif phase==590 then
  call RemoveUnit(udg_O346Unit)
  call RemoveUnit(udg_O346Carrier)
 endif
 call O346Mark("sample")
 set udg_O346Tick=udg_O346Tick+1
 if ModuloInteger(udg_O346Tick,600)==0 then
  set udg_O346Scene=udg_O346Scene+1
  if udg_O346Scene==1 then
   call O346Mark("complete")
   call PreloadGenEnd("order346.txt")
   call PauseTimer(udg_O346Timer)
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger orders=CreateTrigger()
 local rect world=GetWorldBounds()
 local integer x=22
 local integer y=R2I(GetRectMinY(world)/32)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call TriggerRegisterPlayerUnitEvent(orders,Player(1),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(orders,Player(1),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(orders,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerRegisterPlayerUnitEvent(orders,Player(0),EVENT_PLAYER_UNIT_ISSUED_TARGET_ORDER,null)
 call TriggerAddAction(orders,function O346Issued)
 call O346Mark("start")
 set udg_O346Timer=CreateTimer()
 call TimerStart(udg_O346Timer,.1,true,function O346Tick)
endfunction
