globals
 unit udg_T224F=null
 unit udg_T224T=null
 unit udg_T224D=null
 timer udg_T224Timer=null
 fogmodifier udg_T224Fog=null
 integer udg_T224Tick=0
 integer udg_T224Scene=0
endglobals
function T224Mark takes string label returns nothing
 local integer visible=0
 local integer shared=0
 if GetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_VISION) then
  set shared=1
 endif
 if IsUnitVisible(udg_T224T,Player(0)) then
  set visible=1
 endif
 call Preload("T224 tick="+I2S(udg_T224Tick)+" scene="+I2S(udg_T224Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_T224F))+" x="+R2S(GetUnitX(udg_T224F))+" y="+R2S(GetUnitY(udg_T224F))+" tx="+R2S(GetUnitX(udg_T224T))+" ty="+R2S(GetUnitY(udg_T224T))+" visible="+I2S(visible)+" shared="+I2S(shared))
endfunction
function T224Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_T224Tick,200)
 if t==0 then
  call FogEnable(false)
  set udg_T224F=CreateUnit(Player(0),'hfoo',64,64,0)
  set udg_T224T=CreateUnit(Player(1),'Ewar',1984,64,90)
  call SetUnitMoveSpeed(udg_T224T,270)
  call SetUnitMoveSpeed(udg_T224F,150)
  call SetUnitAcquireRange(udg_T224F,0)
  call SetUnitAcquireRange(udg_T224T,0)
  if udg_T224Scene==2 then
   call SetUnitPosition(udg_T224T,176,64)
  endif
  call SetHeroLevel(udg_T224T,10,false)
  call SelectHeroSkill(udg_T224T,'AEbl')
  call SelectHeroSkill(udg_T224T,'AEbl')
  call SelectHeroSkill(udg_T224T,'AEbl')
  call SetUnitState(udg_T224T,UNIT_STATE_MANA,1000)
  call IssueImmediateOrder(udg_T224T,"holdposition")
  call T224Mark("begin")
 elseif t==1 and udg_T224Scene==0 then
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif t==5 then
  if IssueTargetOrder(udg_T224F,"attack",udg_T224T) then
   call T224Mark("accepted")
  else
   call T224Mark("rejected")
  endif
 elseif t==6 and udg_T224Scene!=2 then
  call T224Mark("before_blink")
  if udg_T224Scene==0 then
   if IssuePointOrder(udg_T224T,"blink",1984,320) then
    call T224Mark("blink_accepted")
   else
    call T224Mark("blink_rejected")
   endif
  elseif udg_T224Scene==1 then
   if IssuePointOrder(udg_T224T,"blink",1984,1184) then
    call T224Mark("blink_accepted")
   else
    call T224Mark("blink_rejected")
   endif
  else
   call SetUnitPosition(udg_T224T,1984,1184)
   call T224Mark("relocated")
  endif
 elseif t==20 and udg_T224Scene==2 then
  call SetUnitPosition(udg_T224T,1984,64)
  call T224Mark("relocated_before_blink")
 elseif t==21 and udg_T224Scene==2 then
  if IssuePointOrder(udg_T224T,"blink",1984,1184) then
   call T224Mark("blink_accepted")
  else
   call T224Mark("blink_rejected")
  endif
 elseif t==190 then
  call IssueImmediateOrder(udg_T224F,"stop")
  call IssueImmediateOrder(udg_T224T,"stop")
 elseif t==195 then
  call RemoveUnit(udg_T224F)
  call RemoveUnit(udg_T224T)
  if udg_T224D!=null then
   call RemoveUnit(udg_T224D)
   set udg_T224D=null
  endif
 elseif t==199 then
  if udg_T224Scene==3 then
   call T224Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_T224Timer)
  endif
  set udg_T224Scene=udg_T224Scene+1
 endif
 if t<195 then
  call T224Mark("sample")
 endif
 set udg_T224Tick=udg_T224Tick+1
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
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 set udg_T224Timer=CreateTimer()
 call TimerStart(udg_T224Timer,0.1,true,function T224Tick)
 call Preload("T224 tick=0 scene=0 label=start order=0")
endfunction
