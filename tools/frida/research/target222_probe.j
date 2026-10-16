globals
 unit udg_T222F=null
 unit udg_T222T=null
 unit udg_T222D=null
 timer udg_T222Timer=null
 fogmodifier udg_T222Fog=null
 integer udg_T222Tick=0
 integer udg_T222Scene=0
endglobals
function T222Mark takes string label returns nothing
 call Preload("T222 tick="+I2S(udg_T222Tick)+" scene="+I2S(udg_T222Scene)+" label="+label+" order="+I2S(GetUnitCurrentOrder(udg_T222F))+" x="+R2S(GetUnitX(udg_T222F))+" y="+R2S(GetUnitY(udg_T222F))+" tx="+R2S(GetUnitX(udg_T222T))+" ty="+R2S(GetUnitY(udg_T222T)))
endfunction
function T222Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_T222Tick,200)
 if t==0 then
  call FogEnable(udg_T222Scene==0 or udg_T222Scene==1)
  set udg_T222F=CreateUnit(Player(0),'hfoo',1344,288,0)
  set udg_T222T=CreateUnit(Player(1),'hfoo',1568,288,90)
  call SetUnitMoveSpeed(udg_T222T,150)
  call SetUnitAcquireRange(udg_T222F,0)
  call SetUnitAcquireRange(udg_T222T,0)
  if udg_T222Scene==3 then
   set udg_T222D=CreateUnit(Player(0),'ushd',1696,800,0)
   call UnitAddAbility(udg_T222D,'Atru')
  endif
  call IssuePointOrder(udg_T222T,"move",1568,1312)
  call T222Mark("begin")
 elseif t==1 and udg_T222Scene==0 then
  call PreloadGenEnd("@START@")
  call PreloadGenClear()
  call PreloadGenStart()
 elseif t==5 then
  if IssueTargetOrder(udg_T222F,"attack",udg_T222T) then
   call T222Mark("accepted")
  else
   call T222Mark("rejected")
  endif
 elseif t==60 then
  call T222Mark("before_loss")
  if udg_T222Scene==0 or udg_T222Scene==1 then
   set udg_T222Fog=CreateFogModifierRadius(Player(0),FOG_OF_WAR_FOGGED,1568,800,900,false,true)
   call FogModifierStart(udg_T222Fog)
  elseif udg_T222Scene==2 or udg_T222Scene==3 then
   call UnitAddAbility(udg_T222T,'Apiv')
  endif
  call T222Mark("after_loss")
 elseif t==63 and udg_T222Scene==1 then
  call DestroyFogModifier(udg_T222Fog)
  set udg_T222Fog=null
  call T222Mark("reacquired")
 elseif t==70 then
  call IssuePointOrder(udg_T222T,"move",GetUnitX(udg_T222T),288)
 elseif t==120 then
  if udg_T222Fog!=null then
   call DestroyFogModifier(udg_T222Fog)
   set udg_T222Fog=null
  endif
  call UnitRemoveAbility(udg_T222T,'Apiv')
  call T222Mark("restored")
 elseif t==190 then
  call IssueImmediateOrder(udg_T222F,"stop")
  call IssueImmediateOrder(udg_T222T,"stop")
 elseif t==195 then
  call RemoveUnit(udg_T222F)
  call RemoveUnit(udg_T222T)
  if udg_T222D!=null then
   call RemoveUnit(udg_T222D)
   set udg_T222D=null
  endif
 elseif t==199 then
  if udg_T222Scene==4 then
   call T222Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_T222Timer)
  endif
  set udg_T222Scene=udg_T222Scene+1
 endif
 if t<195 then
  call T222Mark("sample")
 endif
 set udg_T222Tick=udg_T222Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetPlayerAlliance(Player(0),Player(1),ALLIANCE_PASSIVE,false)
 call SetPlayerAlliance(Player(1),Player(0),ALLIANCE_PASSIVE,false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 set udg_T222Timer=CreateTimer()
 call TimerStart(udg_T222Timer,0.1,true,function T222Tick)
 call Preload("T222 tick=0 scene=0 label=start order=0")
endfunction
