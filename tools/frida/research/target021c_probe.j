globals
 timer udg_T021Timer=null
 integer udg_T021Tick=0
 integer udg_T021Scene=-1
 integer udg_T021Local=0
 unit udg_T021F=null
 unit udg_T021T=null
 unit array udg_T021C
 fogmodifier udg_T021Fog=null
endglobals
// TARGET-02.1 live probe c (research tool): probe b plus a frozen noon time of day (SetFloatGameState
// GAME_STATE_TIME_OF_DAY 12, SetTimeOfDayScale 0) so the fog scene's target is visible at order time.
// Probe b's fog scene rejected Smart because the target was not visible at night (kept as evidence).
// Scene 0 contend: 36 player-0 footmen each receive public Smart on one walking player-0 target in the
//   same JASS callback (one physical target group each, one shared target-group scheduler row).
// Scene 1 fog: fog enabled; one footman Smart-follows a walking neutral-passive footman; a player-0
//   FOG_OF_WAR_FOGGED radius-900 modifier (afterUnits=true) covers the target side from local tick 60 to 120.
// One public sample per 0.1 s tick; stop at 190, removal at 195. Only public JASS natives are used.
function T021Mark takes string label returns nothing
 call Preload("T021 tick="+I2S(udg_T021Tick)+" s="+I2S(udg_T021Scene)+" l="+I2S(udg_T021Local)+" label="+label)
endfunction
function T021Pos takes unit u returns string
 if u==null then
  return "none"
 endif
 return R2S(GetUnitX(u))+","+R2S(GetUnitY(u))+","+I2S(GetUnitCurrentOrder(u))
endfunction
function T021Sample takes nothing returns nothing
 local integer v=0
 local integer i=0
 local string c=""
 if udg_T021T!=null and udg_T021F!=null and IsUnitVisible(udg_T021T,GetOwningPlayer(udg_T021F)) then
  set v=1
 endif
 call Preload("T021 tick="+I2S(udg_T021Tick)+" s="+I2S(udg_T021Scene)+" l="+I2S(udg_T021Local)+" label=sample f="+T021Pos(udg_T021F)+" t="+T021Pos(udg_T021T)+" v="+I2S(v))
 if udg_T021Scene==0 and ModuloInteger(udg_T021Local,10)==0 then
  loop
   exitwhen i==36
   set c=c+" "+T021Pos(udg_T021C[i])
   set i=i+1
   if ModuloInteger(i,6)==0 then
    call Preload("T021 tick="+I2S(udg_T021Tick)+" s="+I2S(udg_T021Scene)+" l="+I2S(udg_T021Local)+" label=crowd row="+I2S(i/6-1)+c)
    set c=""
   endif
  endloop
 endif
endfunction
function T021Cleanup takes nothing returns nothing
 local integer i=0
 call T021Mark("begin-cleanup")
 if udg_T021F!=null then
  call RemoveUnit(udg_T021F)
 endif
 if udg_T021T!=null then
  call RemoveUnit(udg_T021T)
 endif
 set udg_T021F=null
 set udg_T021T=null
 loop
  exitwhen i==36
  if udg_T021C[i]!=null then
   call RemoveUnit(udg_T021C[i])
   set udg_T021C[i]=null
  endif
  set i=i+1
 endloop
 if udg_T021Fog!=null then
  call DestroyFogModifier(udg_T021Fog)
  set udg_T021Fog=null
 endif
 call FogEnable(false)
 call T021Mark("end-cleanup")
endfunction
function T021Setup takes integer s returns nothing
 local integer i=0
 call T021Mark("begin-setup")
 if s==0 then
  set udg_T021T=CreateUnit(Player(0),'hfoo',1568.0,288.0,90.0)
  loop
   exitwhen i==36
   set udg_T021C[i]=CreateUnit(Player(0),'hfoo',160.0+I2R(ModuloInteger(i,6))*64.0,160.0+I2R(i/6)*64.0,0.0)
   set i=i+1
  endloop
  set udg_T021F=udg_T021C[0]
 else
  call FogEnable(true)
  call FogMaskEnable(false)
  call SetPlayerAlliance(Player(0),Player(PLAYER_NEUTRAL_PASSIVE),ALLIANCE_PASSIVE,true)
  call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE),Player(0),ALLIANCE_PASSIVE,true)
  set udg_T021F=CreateUnit(Player(0),'hfoo',480.0,288.0,0.0)
  set udg_T021T=CreateUnit(Player(PLAYER_NEUTRAL_PASSIVE),'hfoo',1568.0,288.0,90.0)
 endif
 call SetUnitMoveSpeed(udg_T021T,150.0)
 call T021Mark("end-setup f="+I2S(GetUnitTypeId(udg_T021F))+" t="+I2S(GetUnitTypeId(udg_T021T))+" fh="+I2S(GetHandleId(udg_T021F))+" th="+I2S(GetHandleId(udg_T021T)))
endfunction
function T021Order takes integer s returns nothing
 local integer i=0
 local integer n=0
 call T021Mark("begin-order")
 if s==0 then
  loop
   exitwhen i==36
   if IssueTargetOrder(udg_T021C[i],"smart",udg_T021T) then
    set n=n+1
   endif
   set i=i+1
  endloop
 elseif IssueTargetOrder(udg_T021F,"smart",udg_T021T) then
  set n=1
 endif
 call T021Mark("end-order accepted="+I2S(n)+" order="+I2S(GetUnitCurrentOrder(udg_T021F)))
endfunction
function T021TargetMotion takes integer s,integer l returns nothing
 local real x=GetUnitX(udg_T021T)
 if l==0 or l==140 then
  call T021Mark("begin-target-move")
  call IssuePointOrder(udg_T021T,"move",x,1312.0)
  call T021Mark("end-target-move")
 elseif l==70 then
  call T021Mark("begin-target-move")
  call IssuePointOrder(udg_T021T,"move",x,288.0)
  call T021Mark("end-target-move")
 endif
 if s==1 and l==60 then
  call T021Mark("begin-fog")
  set udg_T021Fog=CreateFogModifierRadius(Player(0),FOG_OF_WAR_FOGGED,1568.0,800.0,900.0,false,true)
  call FogModifierStart(udg_T021Fog)
  call T021Mark("end-fog")
 endif
 if s==1 and l==120 then
  call T021Mark("begin-unfog")
  call DestroyFogModifier(udg_T021Fog)
  set udg_T021Fog=null
  call T021Mark("end-unfog")
 endif
endfunction
function T021Tick takes nothing returns nothing
 local integer s
 local integer l
 set udg_T021Tick=udg_T021Tick+1
 if udg_T021Tick<20 then
  return
 endif
 set s=(udg_T021Tick-20)/200
 set l=ModuloInteger(udg_T021Tick-20,200)
 if s>=@SCENES@ then
  call T021Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_T021Timer)
  return
 endif
 set udg_T021Scene=s
 set udg_T021Local=l
 if l==0 then
  call T021Setup(s)
 endif
 if l<195 then
  call T021TargetMotion(s,l)
 endif
 if l==5 then
  call T021Order(s)
 endif
 if l==190 then
  call T021Mark("begin-stop")
  call IssueImmediateOrder(udg_T021F,"stop")
  call IssueImmediateOrder(udg_T021T,"stop")
  call T021Mark("end-stop")
 endif
 if l<195 then
  call T021Sample()
 endif
 if l==195 then
  call T021Cleanup()
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12.0)
 call SetTimeOfDayScale(0.0)
 call SetCameraPosition(1024.0,1024.0)
 call T021Mark("start")
 set udg_T021Timer=CreateTimer()
 call TimerStart(udg_T021Timer,0.1,true,function T021Tick)
endfunction
