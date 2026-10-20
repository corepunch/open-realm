globals
 unit udg_O344Unit=null
 unit udg_O344Carrier=null
 timer udg_O344Timer=null
 integer udg_O344Tick=0
 integer udg_O344Scene=0
endglobals
function O344Mark takes string label returns nothing
 local integer loaded=0
 if IsUnitLoaded(udg_O344Unit) then
  set loaded=1
 endif
 call Preload("O344 tick="+I2S(udg_O344Tick)+" scene="+I2S(udg_O344Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_O344Unit))+" x="+R2S(GetUnitX(udg_O344Unit))+" y="+R2S(GetUnitY(udg_O344Unit))+" loaded="+I2S(loaded))
endfunction
function O344Issued takes nothing returns nothing
 call O344Mark("event_"+I2S(GetIssuedOrderId()))
endfunction
function O344Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_O344Tick,180)
 if phase==0 then
  if udg_O344Scene==2 then
   call StartCampaignAI(Player(1),"order344.ai")
  endif
  if udg_O344Scene==0 then
   set udg_O344Unit=CreateUnit(Player(0),'hfoo',272,304,90)
  else
   set udg_O344Unit=CreateUnit(Player(1),'hfoo',272,304,90)
  endif
  call SetUnitAcquireRange(udg_O344Unit,0)
  if udg_O344Scene==2 then
   set udg_O344Carrier=CreateUnit(Player(1),'nzep',400,400,90)
  endif
 elseif phase==5 then
  call IssuePointOrder(udg_O344Unit,"move",1008,1040)
  call O344Mark("issued")
 elseif phase==10 then
  if udg_O344Scene==3 then
   call RemoveGuardPosition(udg_O344Unit)
  elseif udg_O344Scene==4 then
   call SetUnitOwner(udg_O344Unit,Player(0),false)
  elseif udg_O344Scene==5 then
   call SetUnitOwner(udg_O344Unit,Player(0),false)
   call SetUnitOwner(udg_O344Unit,Player(1),false)
   call IssuePointOrder(udg_O344Unit,"move",1008,1040)
  endif
  call O344Mark("ownership")
 elseif phase==175 then
  call RemoveUnit(udg_O344Unit)
  call RemoveUnit(udg_O344Carrier)
  set udg_O344Carrier=null
 endif
 call O344Mark("sample")
 set udg_O344Tick=udg_O344Tick+1
 if ModuloInteger(udg_O344Tick,180)==0 then
  set udg_O344Scene=udg_O344Scene+1
  if udg_O344Scene==6 then
   call O344Mark("complete")
   call PreloadGenEnd("order344.txt")
   call PauseTimer(udg_O344Timer)
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger orders=CreateTrigger()
 local integer x=31
 local integer y=31
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call TriggerRegisterPlayerUnitEvent(orders,Player(1),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddAction(orders,function O344Issued)
 loop
  exitwhen y>32
  set x=31
  loop
   exitwhen x>32
   call SetTerrainPathable(I2R(x*32+16),I2R(y*32+16),PATHING_TYPE_WALKABILITY,false)
   set x=x+1
  endloop
  set y=y+1
 endloop
 call O344Mark("start")
 set udg_O344Timer=CreateTimer()
 call TimerStart(udg_O344Timer,.1,true,function O344Tick)
endfunction
