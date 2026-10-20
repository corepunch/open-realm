globals
 timer udg_F187Timer=null
 integer udg_F187Tick=0
 integer udg_F187Scene=7
 integer udg_F187Local=0
 integer udg_F187Dir=1
 unit udg_F187F=null
 unit udg_F187T=null
 unit array udg_F187C
endglobals
// Payoff187 public ground Follow to a flying target; bounded scene7 replay.
// TARGET-02.1 live probe (research tool). Flat 64x64-fine arena with an unwalkable wall at fine x30..33,
// y0..49 (world 960..1088 x 0..1600); gap at the north. Each scene creates a fresh follower west of the
// wall and a target east of it, issues one public target order at local tick 5, moves the target with
// public orders/setters, records one public sample per 0.1 s tick, stops at 190 and removes at 195.
// Scenes: 0 smart_walk, 1 smart_step, 2 move_walk, 3 attack_walk, 4 holylight_walk, 5 fly_walk,
// 6 amph_walk, 7 ground_follows_flyer, 8 smart_crowd. Only public JASS natives are used.
function F187Mark takes string label returns nothing
 call Preload("F187 tick="+I2S(udg_F187Tick)+" s="+I2S(udg_F187Scene)+" l="+I2S(udg_F187Local)+" label="+label)
endfunction
function F187Pos takes unit u returns string
 if u==null then
  return "none"
 endif
 return R2S(GetUnitX(u))+","+R2S(GetUnitY(u))+","+I2S(GetUnitCurrentOrder(u))+","+R2S(GetUnitState(u,UNIT_STATE_LIFE))
endfunction
function F187Sample takes nothing returns nothing
 local integer v=0
 if udg_F187T!=null and udg_F187F!=null and IsUnitVisible(udg_F187T,GetOwningPlayer(udg_F187F)) then
  set v=1
 endif
 call Preload("F187 tick="+I2S(udg_F187Tick)+" s="+I2S(udg_F187Scene)+" l="+I2S(udg_F187Local)+" label=sample f="+F187Pos(udg_F187F)+" t="+F187Pos(udg_F187T)+" v="+I2S(v))
endfunction
function F187Cleanup takes nothing returns nothing
 local integer i=0
 call F187Mark("begin-cleanup")
 if udg_F187F!=null then
  call RemoveUnit(udg_F187F)
 endif
 if udg_F187T!=null then
  call RemoveUnit(udg_F187T)
 endif
 set udg_F187F=null
 set udg_F187T=null
 loop
  exitwhen i==8
  if udg_F187C[i]!=null then
   call RemoveUnit(udg_F187C[i])
   set udg_F187C[i]=null
  endif
  set i=i+1
 endloop
 call F187Mark("end-cleanup")
endfunction
function F187Setup takes integer s returns nothing
 local integer i=0
 local integer ft='hfoo'
 local integer tt='hfoo'
 local player tp=Player(0)
 local real tx=1568.0
 if s==4 then
  set ft='Hpal'
 elseif s==5 then
  set ft='hgry'
 elseif s==6 then
  set ft='nmyr'
 endif
 if s==3 then
  set tp=Player(1)
 endif
 if s==7 then
  set tt='hgry'
  set tx=1024.0
 endif
 call F187Mark("begin-setup")
 set udg_F187F=CreateUnit(Player(0),ft,480.0,288.0,0.0)
 set udg_F187T=CreateUnit(tp,tt,tx,288.0,90.0)
 call SetUnitMoveSpeed(udg_F187T,150.0)
 if s==4 then
  call SelectHeroSkill(udg_F187F,'AHhb')
  call SetUnitState(udg_F187F,UNIT_STATE_MANA,GetUnitState(udg_F187F,UNIT_STATE_MAX_MANA))
  call SetUnitState(udg_F187T,UNIT_STATE_LIFE,30.0)
 endif
 if s==8 then
  loop
   exitwhen i==8
   set udg_F187C[i]=CreateUnit(Player(0),'hfoo',320.0+I2R(ModuloInteger(i,4))*80.0,640.0+I2R(i/4)*80.0,0.0)
   set i=i+1
  endloop
 endif
 set udg_F187Dir=1
 call F187Mark("end-setup f="+I2S(GetUnitTypeId(udg_F187F))+" t="+I2S(GetUnitTypeId(udg_F187T))+" fh="+I2S(GetHandleId(udg_F187F))+" th="+I2S(GetHandleId(udg_F187T)))
endfunction
function F187Order takes integer s returns nothing
 local boolean ok=false
 call F187Mark("begin-order")
 if s==0 or s==1 or s==5 or s==6 or s==7 or s==8 then
  set ok=IssueTargetOrder(udg_F187F,"smart",udg_F187T)
 elseif s==2 then
  set ok=IssueTargetOrder(udg_F187F,"move",udg_F187T)
 elseif s==3 then
  set ok=IssueTargetOrder(udg_F187F,"attack",udg_F187T)
 elseif s==4 then
  set ok=IssueTargetOrder(udg_F187F,"holybolt",udg_F187T)
 endif
 if ok then
  call F187Mark("end-order accepted=1 order="+I2S(GetUnitCurrentOrder(udg_F187F)))
 else
  call F187Mark("end-order accepted=0 order="+I2S(GetUnitCurrentOrder(udg_F187F)))
 endif
endfunction
function F187TargetMotion takes integer s,integer l returns nothing
 local real x=GetUnitX(udg_F187T)
 local integer i=0
 if s==1 then
  if l>=2 and l<=186 and ModuloInteger(l,2)==0 then
   if (udg_F187Dir==1 and GetUnitY(udg_F187T)>=1280.0) or (udg_F187Dir==-1 and GetUnitY(udg_F187T)<=320.0) then
    set udg_F187Dir=-udg_F187Dir
   endif
   call F187Mark("begin-step dir="+I2S(udg_F187Dir))
   call SetUnitY(udg_F187T,GetUnitY(udg_F187T)+64.0*I2R(udg_F187Dir))
   call F187Mark("end-step y="+R2S(GetUnitY(udg_F187T)))
  endif
 elseif l==0 or l==140 then
  call F187Mark("begin-target-move")
  call IssuePointOrder(udg_F187T,"move",x,1312.0)
  call F187Mark("end-target-move")
 elseif l==70 then
  call F187Mark("begin-target-move")
  call IssuePointOrder(udg_F187T,"move",x,288.0)
  call F187Mark("end-target-move")
 endif
 if s==4 and (l==70 or l==140) then
  call F187Mark("begin-reissue")
  call SetUnitState(udg_F187T,UNIT_STATE_LIFE,30.0)
  call F187Order(s)
  call F187Mark("end-reissue")
 endif
 if s==8 and l>=5 and ModuloInteger(l-5,30)==0 then
  call F187Mark("begin-crowd")
  loop
   exitwhen i==8
   if ModuloInteger((l-5)/30,2)==0 then
    call IssuePointOrder(udg_F187C[i],"move",1600.0+I2R(ModuloInteger(i,4))*64.0,1760.0+I2R(i/4)*64.0)
   else
    call IssuePointOrder(udg_F187C[i],"move",320.0+I2R(ModuloInteger(i,4))*80.0,640.0+I2R(i/4)*80.0)
   endif
   set i=i+1
  endloop
  call F187Mark("end-crowd")
 endif
endfunction
function F187Tick takes nothing returns nothing
 local integer s
 local integer l
 set udg_F187Tick=udg_F187Tick+1
 if udg_F187Tick<20 then
  return
 endif
 set s=7
 set l=ModuloInteger(udg_F187Tick-20,200)
 if udg_F187Tick>=220 then
  call F187Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_F187Timer)
  return
 endif
 set udg_F187Scene=s
 set udg_F187Local=l
 if l==0 then
  call F187Setup(s)
 endif
 if l<195 then
  call F187TargetMotion(s,l)
 endif
 if l==5 then
  call F187Order(s)
 endif
 if l==190 then
  call F187Mark("begin-stop")
  call IssueImmediateOrder(udg_F187F,"stop")
  call IssueImmediateOrder(udg_F187T,"stop")
  call F187Mark("end-stop")
 endif
 if l<195 then
  call F187Sample()
 endif
 if l==195 then
  call F187Cleanup()
 endif
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_VISION,false)
 call SetCameraPosition(1024.0,1024.0)
 call F187Mark("start")
 set udg_F187Timer=CreateTimer()
 call TimerStart(udg_F187Timer,0.1,true,function F187Tick)
endfunction
