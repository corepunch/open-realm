globals
 unit udg_T371Unit=null
 unit udg_T371Unused=null
 timer udg_T371Timer=null
 integer udg_T371Tick=0
 integer udg_T371Scene=0
 integer udg_T371Accepted=0
endglobals
function T371Mark takes string label returns nothing
 local integer loaded=0
 if IsUnitLoaded(udg_T371Unit) then
  set loaded=1
 endif
 call Preload("T371 tick="+I2S(udg_T371Tick)+" scene="+I2S(udg_T371Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_T371Unit))+" x="+R2S(GetUnitX(udg_T371Unit))+" y="+R2S(GetUnitY(udg_T371Unit))+" accepted="+I2S(udg_T371Accepted)+" loaded="+I2S(loaded))
endfunction
function T371Issued takes nothing returns nothing
 call T371Mark("event_"+I2S(GetIssuedOrderId()))
endfunction
function T371Tick takes nothing returns nothing
 local integer phase=ModuloInteger(udg_T371Tick,180)
 if phase==0 then
  set udg_T371Accepted=0
  set udg_T371Unit=CreateUnit(Player(udg_T371Scene),'hfoo',272,304,90)
  call SetUnitAcquireRange(udg_T371Unit,0)
  call T371Mark("created")
 elseif phase==5 then
  call T371Mark("issue-before")
  if IssuePointOrder(udg_T371Unit,"attackground",1008,1040) then
   set udg_T371Accepted=1
  else
   set udg_T371Accepted=0
  endif
  call T371Mark("issue-after")
 elseif phase==175 then
  call RemoveUnit(udg_T371Unit)
 endif
 call T371Mark("sample")
 set udg_T371Tick=udg_T371Tick+1
 if ModuloInteger(udg_T371Tick,180)==0 then
  set udg_T371Scene=udg_T371Scene+1
  if udg_T371Scene==2 then
   call T371Mark("complete")
   call PreloadGenEnd("target371-ground-ai.txt")
   call PauseTimer(udg_T371Timer)
  endif
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 local trigger orders=CreateTrigger()
 local integer x=29
 local integer y=30
 call SetRandomSeed(1371)
 call FogEnable(false)
 call FogMaskEnable(false)
 call PreloadGenClear()
 call PreloadGenStart()
 call TriggerRegisterPlayerUnitEvent(orders,Player(1),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)
 call TriggerAddAction(orders,function T371Issued)
 loop
  exitwhen y>34
  set x=29
  loop
   exitwhen x>33
   call SetTerrainPathable(I2R(x*32+16),I2R(y*32+16),PATHING_TYPE_WALKABILITY,false)
   set x=x+1
  endloop
  set y=y+1
 endloop
 call T371Mark("start")
 set udg_T371Timer=CreateTimer()
 call TimerStart(udg_T371Timer,.1,true,function T371Tick)
endfunction
