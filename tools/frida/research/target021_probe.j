globals
 timer udg_T021Timer=null
 integer udg_T021Tick=0
 integer udg_T021Scene=-1
 integer udg_T021Local=0
 integer udg_T021Dir=1
 unit udg_T021F=null
 unit udg_T021T=null
 unit array udg_T021C
endglobals
// TARGET-02.1 live probe (research tool). Flat 64x64-fine arena with an unwalkable wall at fine x30..33,
// y0..49 (world 960..1088 x 0..1600); gap at the north. Each scene creates a fresh follower west of the
// wall and a target east of it, issues one public target order at local tick 5, moves the target with
// public orders/setters, records one public sample per 0.1 s tick, stops at 190 and removes at 195.
// Scenes: 0 smart_walk, 1 smart_step, 2 move_walk, 3 attack_walk, 4 holylight_walk, 5 fly_walk,
// 6 amph_walk, 7 ground_follows_flyer, 8 smart_crowd. Only public JASS natives are used.
function T021Mark takes string label returns nothing
 call Preload("T021 tick="+I2S(udg_T021Tick)+" s="+I2S(udg_T021Scene)+" l="+I2S(udg_T021Local)+" label="+label)
endfunction
function T021Pos takes unit u returns string
 if u==null then
  return "none"
 endif
 return R2S(GetUnitX(u))+","+R2S(GetUnitY(u))+","+I2S(GetUnitCurrentOrder(u))+","+R2S(GetUnitState(u,UNIT_STATE_LIFE))
endfunction
function T021Sample takes nothing returns nothing
 local integer v=0
 if udg_T021T!=null and udg_T021F!=null and IsUnitVisible(udg_T021T,GetOwningPlayer(udg_T021F)) then
  set v=1
 endif
 call Preload("T021 tick="+I2S(udg_T021Tick)+" s="+I2S(udg_T021Scene)+" l="+I2S(udg_T021Local)+" label=sample f="+T021Pos(udg_T021F)+" t="+T021Pos(udg_T021T)+" v="+I2S(v))
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
  exitwhen i==8
  if udg_T021C[i]!=null then
   call RemoveUnit(udg_T021C[i])
   set udg_T021C[i]=null
  endif
  set i=i+1
 endloop
 call T021Mark("end-cleanup")
endfunction
function T021Setup takes integer s returns nothing
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
 call T021Mark("begin-setup")
 set udg_T021F=CreateUnit(Player(0),ft,480.0,288.0,0.0)
 set udg_T021T=CreateUnit(tp,tt,tx,288.0,90.0)
 call SetUnitMoveSpeed(udg_T021T,150.0)
 if s==4 then
  call SelectHeroSkill(udg_T021F,'AHhb')
  call SetUnitState(udg_T021F,UNIT_STATE_MANA,GetUnitState(udg_T021F,UNIT_STATE_MAX_MANA))
  call SetUnitState(udg_T021T,UNIT_STATE_LIFE,30.0)
 endif
 if s==8 then
  loop
   exitwhen i==8
   set udg_T021C[i]=CreateUnit(Player(0),'hfoo',320.0+I2R(ModuloInteger(i,4))*80.0,640.0+I2R(i/4)*80.0,0.0)
   set i=i+1
  endloop
 endif
 set udg_T021Dir=1
 call T021Mark("end-setup f="+I2S(GetUnitTypeId(udg_T021F))+" t="+I2S(GetUnitTypeId(udg_T021T))+" fh="+I2S(GetHandleId(udg_T021F))+" th="+I2S(GetHandleId(udg_T021T)))
endfunction
function T021Order takes integer s returns nothing
 local boolean ok=false
 call T021Mark("begin-order")
 if s==0 or s==1 or s==5 or s==6 or s==7 or s==8 then
  set ok=IssueTargetOrder(udg_T021F,"smart",udg_T021T)
 elseif s==2 then
  set ok=IssueTargetOrder(udg_T021F,"move",udg_T021T)
 elseif s==3 then
  set ok=IssueTargetOrder(udg_T021F,"attack",udg_T021T)
 elseif s==4 then
  set ok=IssueTargetOrder(udg_T021F,"holybolt",udg_T021T)
 endif
 if ok then
  call T021Mark("end-order accepted=1 order="+I2S(GetUnitCurrentOrder(udg_T021F)))
 else
  call T021Mark("end-order accepted=0 order="+I2S(GetUnitCurrentOrder(udg_T021F)))
 endif
endfunction
function T021TargetMotion takes integer s,integer l returns nothing
 local real x=GetUnitX(udg_T021T)
 local integer i=0
 if s==1 then
  if l>=2 and l<=186 and ModuloInteger(l,2)==0 then
   if (udg_T021Dir==1 and GetUnitY(udg_T021T)>=1280.0) or (udg_T021Dir==-1 and GetUnitY(udg_T021T)<=320.0) then
    set udg_T021Dir=-udg_T021Dir
   endif
   call T021Mark("begin-step dir="+I2S(udg_T021Dir))
   call SetUnitY(udg_T021T,GetUnitY(udg_T021T)+64.0*I2R(udg_T021Dir))
   call T021Mark("end-step y="+R2S(GetUnitY(udg_T021T)))
  endif
 elseif l==0 or l==140 then
  call T021Mark("begin-target-move")
  call IssuePointOrder(udg_T021T,"move",x,1312.0)
  call T021Mark("end-target-move")
 elseif l==70 then
  call T021Mark("begin-target-move")
  call IssuePointOrder(udg_T021T,"move",x,288.0)
  call T021Mark("end-target-move")
 endif
 if s==4 and (l==70 or l==140) then
  call T021Mark("begin-reissue")
  call SetUnitState(udg_T021T,UNIT_STATE_LIFE,30.0)
  call T021Order(s)
  call T021Mark("end-reissue")
 endif
 if s==8 and l>=5 and ModuloInteger(l-5,30)==0 then
  call T021Mark("begin-crowd")
  loop
   exitwhen i==8
   if ModuloInteger((l-5)/30,2)==0 then
    call IssuePointOrder(udg_T021C[i],"move",1600.0+I2R(ModuloInteger(i,4))*64.0,1760.0+I2R(i/4)*64.0)
   else
    call IssuePointOrder(udg_T021C[i],"move",320.0+I2R(ModuloInteger(i,4))*80.0,640.0+I2R(i/4)*80.0)
   endif
   set i=i+1
  endloop
  call T021Mark("end-crowd")
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
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_SHARED_VISION,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_VISION,false)
 call SetCameraPosition(1024.0,1024.0)
 call T021Mark("start")
 set udg_T021Timer=CreateTimer()
 call TimerStart(udg_T021Timer,0.1,true,function T021Tick)
endfunction
