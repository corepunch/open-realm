globals
 unit udg_T223F=null
 unit udg_T223T=null
 unit udg_T223D=null
 timer udg_T223Timer=null
 fogmodifier udg_T223Fog=null
 integer udg_T223Tick=0
 integer udg_T223Scene=0
endglobals
function T223Mark takes string label returns nothing
 local integer visible=0
 local integer shared=0
 if GetPlayerAlliance(Player(1),Player(0),ALLIANCE_SHARED_VISION) then
  set shared=1
 endif
 if IsUnitVisible(udg_T223T,Player(0)) then
  set visible=1
 endif
 call Preload("T223 tick="+I2S(udg_T223Tick)+" scene="+I2S(udg_T223Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_T223F))+" x="+R2S(GetUnitX(udg_T223F))+" y="+R2S(GetUnitY(udg_T223F))+" tx="+R2S(GetUnitX(udg_T223T))+" ty="+R2S(GetUnitY(udg_T223T))+" visible="+I2S(visible)+" shared="+I2S(shared))
endfunction
function T223Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_T223Tick,200)
 if t==0 then
  call FogEnable(false)
  set udg_T223F=CreateUnit(Player(0),'hfoo',1344,288,0)
  set udg_T223T=CreateUnit(Player(1),'hfoo',1456,288,90)
  call SetUnitMoveSpeed(udg_T223T,270)
  call SetUnitMoveSpeed(udg_T223F,150)
  call SetUnitAcquireRange(udg_T223F,0)
  call SetUnitAcquireRange(udg_T223T,0)
  if udg_T223Scene==1 then
   set udg_T223D=CreateUnit(Player(0),'ushd',1696,1568,0)
   call UnitAddAbility(udg_T223D,'Atru')
  endif
  call T223Mark("begin")
 elseif t==1 and udg_T223Scene==0 then
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif t==5 then
  if IssueTargetOrder(udg_T223F,"attack",udg_T223T) then
   call T223Mark("accepted")
  else
   call T223Mark("rejected")
  endif
  call IssuePointOrder(udg_T223T,"move",1456,1856)
 elseif t==60 then
  call T223Mark("before_loss")
  if udg_T223Scene==0 or udg_T223Scene==1 then
   call UnitAddAbility(udg_T223T,'Apiv')
  elseif udg_T223Scene==2 then
   call ShowUnit(udg_T223T,false)
  elseif udg_T223Scene==3 then
   call KillUnit(udg_T223T)
  elseif udg_T223Scene==4 then
   call RemoveUnit(udg_T223T)
  endif
  call T223Mark("after_loss")
 elseif t==120 then
  if udg_T223Fog!=null then
   call DestroyFogModifier(udg_T223Fog)
   set udg_T223Fog=null
  endif
  call UnitRemoveAbility(udg_T223T,'Apiv')
  call ShowUnit(udg_T223T,true)
  call T223Mark("restored")
 elseif t==190 then
  call IssueImmediateOrder(udg_T223F,"stop")
  call IssueImmediateOrder(udg_T223T,"stop")
 elseif t==195 then
  call RemoveUnit(udg_T223F)
  call RemoveUnit(udg_T223T)
  if udg_T223D!=null then
   call RemoveUnit(udg_T223D)
   set udg_T223D=null
  endif
 elseif t==199 then
  if udg_T223Scene==4 then
   call T223Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_T223Timer)
  endif
  set udg_T223Scene=udg_T223Scene+1
 endif
 if t<195 then
  call T223Mark("sample")
 endif
 set udg_T223Tick=udg_T223Tick+1
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
 set udg_T223Timer=CreateTimer()
 call TimerStart(udg_T223Timer,0.1,true,function T223Tick)
 call Preload("T223 tick=0 scene=0 label=start order=0")
endfunction
