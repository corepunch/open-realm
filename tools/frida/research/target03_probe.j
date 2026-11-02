globals
 timer udg_T03Timer=null
 integer udg_T03Tick=0
 integer udg_T03Scene=-1
 integer udg_T03Local=0
 unit udg_T03F=null
 unit udg_T03T=null
 unit udg_T03Z=null
 fogmodifier udg_T03Fog=null
endglobals
// TARGET-03.1/03.2 live probe (research tool), same walled arena as target021_probe.j, frozen noon.
// Every scene: player-0 footman F receives public Smart on target T at local tick 5 (scene 14: 65); T walks
// north/south east of the wall with public Move orders (l 0/70/140); one loss producer at l 60, an undo at
// l 120 where public, explicit Stop at 190, removal at 195. One public sample per 0.1 s.
// F starts east of the wall at (1344,288) (persistent Follow before l 60) except scene 1 (west, (480,288):
// still approaching at l 60). Target owner / producer:
//  0 fog_persist      neutral passive, fog on, player-0 FOGGED radius-900 modifier 60..120
//  1 fog_approach     same, follower west of the wall
//  2 fog_shared       neutral passive sharing vision with player 0, same modifier
//  3 invis_np         neutral passive, fog off, UnitAddAbility Apiv 60, UnitRemoveAbility 120
//  4 invis_own        player 0, Apiv 60/120
//  5 invis_shared     neutral passive sharing vision with player 0, Apiv 60/120
//  6 remove           player 0, RemoveUnit 60
//  7 kill             player 0, KillUnit 60
//  8 hide             player 0, ShowUnit false 60 / true 120
//  9 cargo            player 0, player-0 Goblin Zeppelin ordered to load T at 50, unload all at 120 (T stands)
// 10 blink            player-0 Warden with Blink, blink order 60 (+256 y)
// 11 owner_enemy      player 0, SetUnitOwner player 1 (enemy, no shared vision) 60, back to player 0 at 120
// 12 pause            player 0, PauseUnit true 60 / false 120
// 13 order_fogged     neutral passive, fog on, modifier from l 2 (order at 5 targets a fogged unit), removed 120
// 14 order_invisible  neutral passive, Apiv at l 2, Smart at 5, UnitRemoveAbility 60, Smart again at 65
// 15 attack_invis     player-1 enemy target, public Attack at 5, Apiv 60/120 (attack internal task, not Follow)
// 16 attack_fog       player-1 enemy target, public Attack at 5, fog on, FOGGED modifier 60..120
function T03Mark takes string label returns nothing
 call Preload("T03 tick="+I2S(udg_T03Tick)+" s="+I2S(udg_T03Scene)+" l="+I2S(udg_T03Local)+" label="+label)
endfunction
function T03Pos takes unit u returns string
 if u==null then
  return "none"
 endif
 return R2S(GetUnitX(u))+","+R2S(GetUnitY(u))+","+I2S(GetUnitCurrentOrder(u))
endfunction
function T03Sample takes nothing returns nothing
 local integer v=0
 local integer h=0
 if udg_T03T!=null and IsUnitVisible(udg_T03T,Player(0)) then
  set v=1
 endif
 if udg_T03T!=null and IsUnitHidden(udg_T03T) then
  set h=1
 endif
 call Preload("T03 tick="+I2S(udg_T03Tick)+" s="+I2S(udg_T03Scene)+" l="+I2S(udg_T03Local)+" label=sample f="+T03Pos(udg_T03F)+" t="+T03Pos(udg_T03T)+" v="+I2S(v)+" h="+I2S(h)+" z="+T03Pos(udg_T03Z))
endfunction
function T03Cleanup takes nothing returns nothing
 call T03Mark("begin-cleanup")
 if udg_T03F!=null then
  call RemoveUnit(udg_T03F)
 endif
 if udg_T03T!=null then
  call RemoveUnit(udg_T03T)
 endif
 if udg_T03Z!=null then
  call RemoveUnit(udg_T03Z)
 endif
 set udg_T03F=null
 set udg_T03T=null
 set udg_T03Z=null
 if udg_T03Fog!=null then
  call DestroyFogModifier(udg_T03Fog)
  set udg_T03Fog=null
 endif
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE),Player(0),ALLIANCE_SHARED_VISION,false)
 call T03Mark("end-cleanup")
endfunction
function T03Np takes integer s returns boolean
 return s==0 or s==1 or s==2 or s==3 or s==5 or s==13 or s==14
endfunction
function T03Setup takes integer s returns nothing
 local player tp=Player(0)
 local integer tt='hfoo'
 local real fx=1344.0
 if s==15 or s==16 then
  set tp=Player(1)
 endif
 call T03Mark("begin-setup")
 if T03Np(s) then
  set tp=Player(PLAYER_NEUTRAL_PASSIVE)
 endif
 if s==10 then
  set tt='Ewar'
 endif
 if s==1 then
  set fx=480.0
 endif
 if s==2 or s==5 then
  call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE),Player(0),ALLIANCE_SHARED_VISION,true)
 endif
 if s==0 or s==1 or s==2 or s==13 or s==16 then
  call FogEnable(true)
 endif
 set udg_T03F=CreateUnit(Player(0),'hfoo',fx,288.0,0.0)
 set udg_T03T=CreateUnit(tp,tt,1568.0,288.0,90.0)
 call SetUnitMoveSpeed(udg_T03T,150.0)
 if s==10 then
  call SelectHeroSkill(udg_T03T,'AEbl')
  call SetUnitState(udg_T03T,UNIT_STATE_MANA,GetUnitState(udg_T03T,UNIT_STATE_MAX_MANA))
 endif
 if s==9 then
  set udg_T03Z=CreateUnit(Player(0),'nzep',1800.0,288.0,180.0)
 endif
 call T03Mark("end-setup t="+I2S(GetUnitTypeId(udg_T03T))+" fh="+I2S(GetHandleId(udg_T03F))+" th="+I2S(GetHandleId(udg_T03T))+" v="+I2S(IntegerTertiaryOp(IsUnitVisible(udg_T03T,Player(0)),1,0)))
endfunction
function T03Order takes nothing returns nothing
 local string o="smart"
 if udg_T03Scene==15 or udg_T03Scene==16 then
  set o="attack"
 endif
 call T03Mark("begin-order v="+I2S(IntegerTertiaryOp(IsUnitVisible(udg_T03T,Player(0)),1,0))+" o="+o)
 if IssueTargetOrder(udg_T03F,o,udg_T03T) then
  call T03Mark("end-order accepted=1 order="+I2S(GetUnitCurrentOrder(udg_T03F)))
 else
  call T03Mark("end-order accepted=0 order="+I2S(GetUnitCurrentOrder(udg_T03F)))
 endif
endfunction
function T03Produce takes integer s,integer l returns nothing
 local boolean ok=false
 if l==2 and s==13 then
  call T03Mark("begin-prefog")
  set udg_T03Fog=CreateFogModifierRadius(Player(0),FOG_OF_WAR_FOGGED,1568.0,800.0,900.0,false,true)
  call FogModifierStart(udg_T03Fog)
  call T03Mark("end-prefog")
 endif
 if l==2 and s==14 then
  call T03Mark("begin-preinvis")
  set ok=UnitAddAbility(udg_T03T,'Apiv')
  call T03Mark("end-preinvis ok="+I2S(IntegerTertiaryOp(ok,1,0)))
 endif
 if l==50 and s==9 then
  call T03Mark("begin-load")
  set ok=IssueTargetOrder(udg_T03Z,"load",udg_T03T)
  call T03Mark("end-load accepted="+I2S(IntegerTertiaryOp(ok,1,0)))
 endif
 if l==60 then
  call T03Mark("begin-produce")
  if s==0 or s==1 or s==2 or s==16 then
   set udg_T03Fog=CreateFogModifierRadius(Player(0),FOG_OF_WAR_FOGGED,1568.0,800.0,900.0,false,true)
   call FogModifierStart(udg_T03Fog)
  elseif s==3 or s==4 or s==5 or s==15 then
   set ok=UnitAddAbility(udg_T03T,'Apiv')
  elseif s==6 then
   call RemoveUnit(udg_T03T)
   set udg_T03T=null
  elseif s==7 then
   call KillUnit(udg_T03T)
  elseif s==8 then
   call ShowUnit(udg_T03T,false)
  elseif s==10 then
   set ok=IssuePointOrder(udg_T03T,"blink",GetUnitX(udg_T03T),GetUnitY(udg_T03T)+256.0)
  elseif s==11 then
   call SetUnitOwner(udg_T03T,Player(1),false)
  elseif s==12 then
   call PauseUnit(udg_T03T,true)
  elseif s==14 then
   set ok=UnitRemoveAbility(udg_T03T,'Apiv')
  endif
  call T03Mark("end-produce ok="+I2S(IntegerTertiaryOp(ok,1,0))+" f="+T03Pos(udg_T03F)+" t="+T03Pos(udg_T03T))
 endif
 if l==65 and s==14 then
  call T03Order()
 endif
 if l==120 then
  call T03Mark("begin-undo")
  if (s==0 or s==1 or s==2 or s==13 or s==16) and udg_T03Fog!=null then
   call DestroyFogModifier(udg_T03Fog)
   set udg_T03Fog=null
  elseif (s==3 or s==4 or s==5 or s==15) and udg_T03T!=null then
   set ok=UnitRemoveAbility(udg_T03T,'Apiv')
  elseif s==8 then
   call ShowUnit(udg_T03T,true)
  elseif s==9 then
   set ok=IssueImmediateOrder(udg_T03Z,"unloadall")
  elseif s==11 then
   call SetUnitOwner(udg_T03T,Player(0),false)
  elseif s==12 then
   call PauseUnit(udg_T03T,false)
  endif
  call T03Mark("end-undo ok="+I2S(IntegerTertiaryOp(ok,1,0))+" f="+T03Pos(udg_T03F)+" t="+T03Pos(udg_T03T))
 endif
endfunction
function T03TargetMotion takes integer s,integer l returns nothing
 if udg_T03T==null or s==9 then
  return
 endif
 if l==0 or l==140 then
  call T03Mark("begin-target-move")
  call IssuePointOrder(udg_T03T,"move",GetUnitX(udg_T03T),1312.0)
  call T03Mark("end-target-move")
 elseif l==70 then
  call T03Mark("begin-target-move")
  call IssuePointOrder(udg_T03T,"move",GetUnitX(udg_T03T),288.0)
  call T03Mark("end-target-move")
 endif
endfunction
function T03Tick takes nothing returns nothing
 local integer s
 local integer l
 set udg_T03Tick=udg_T03Tick+1
 if udg_T03Tick<20 then
  return
 endif
 set s=(udg_T03Tick-20)/200
 set l=ModuloInteger(udg_T03Tick-20,200)
 if s>=@SCENES@ then
  call T03Mark("complete")
  call PreloadGenEnd("@OUTPUT@")
  call PauseTimer(udg_T03Timer)
  return
 endif
 set udg_T03Scene=s
 set udg_T03Local=l
 if l==0 then
  call T03Setup(s)
 endif
 if l<195 then
  call T03TargetMotion(s,l)
  call T03Produce(s,l)
 endif
 if l==5 then
  call T03Order()
 endif
 if l==190 then
  call T03Mark("begin-stop")
  call IssueImmediateOrder(udg_T03F,"stop")
  if udg_T03T!=null then
   call IssueImmediateOrder(udg_T03T,"stop")
  endif
  call T03Mark("end-stop")
 endif
 if l<195 then
  call T03Sample()
 endif
 if l==195 then
  call T03Cleanup()
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
 call SetPlayerAlliance(Player(0),Player(PLAYER_NEUTRAL_PASSIVE),ALLIANCE_PASSIVE,true)
 call SetPlayerAlliance(Player(PLAYER_NEUTRAL_PASSIVE),Player(0),ALLIANCE_PASSIVE,true)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12.0)
 call SetTimeOfDayScale(0.0)
 call SetCameraPosition(1024.0,1024.0)
 call T03Mark("start")
 set udg_T03Timer=CreateTimer()
 call TimerStart(udg_T03Timer,0.1,true,function T03Tick)
endfunction
