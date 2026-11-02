globals
 unit udg_A323Unit=null
 timer udg_A323Timer=null
 integer udg_A323Tick=0
 integer udg_A323Scene=0
endglobals
function A323Mark takes string label returns nothing
 call Preload("S323 tick="+I2S(udg_A323Tick)+" scene="+I2S(udg_A323Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_A323Unit))+" order="+I2S(GetUnitCurrentOrder(udg_A323Unit))+" buff="+I2S(GetUnitAbilityLevel(udg_A323Unit,'BHav')))
 if udg_A323Tick>0 then
  call PreloadGenEnd("@OUTPUT@")
 endif
endfunction
function A323Tick takes nothing returns nothing
 local integer t=ModuloInteger(udg_A323Tick,50)
 local boolean ok=false
 if t==0 then
  set udg_A323Unit=CreateUnit(Player(0),'hfoo',640,1024,0)
  call SetUnitAcquireRange(udg_A323Unit,0)
  if udg_A323Scene==3 then
   call UnitAddAbility(udg_A323Unit,'A324')
  else
   call UnitAddAbility(udg_A323Unit,'A323')
  endif
  call A323Mark("created")
 elseif t==5 then
  set ok=IssueImmediateOrder(udg_A323Unit,"avatar")
  if ok then
   call A323Mark("accepted")
  else
   call A323Mark("rejected")
  endif
 elseif t==7 then
  if udg_A323Scene==1 then
   call PauseUnit(udg_A323Unit,true)
  elseif udg_A323Scene==2 then
   call IssueImmediateOrder(udg_A323Unit,"stop")
  elseif udg_A323Scene==7 then
   call UnitRemoveAbility(udg_A323Unit,'A323')
  elseif udg_A323Scene==4 then
   call KillUnit(udg_A323Unit)
  elseif udg_A323Scene==5 then
   call RemoveUnit(udg_A323Unit)
  elseif udg_A323Scene==6 then
   call UnitRemoveAbility(udg_A323Unit,'BHav')
  endif
  call A323Mark("action")
 elseif t==20 and udg_A323Scene==1 then
  call PauseUnit(udg_A323Unit,false)
  call A323Mark("resume")
 elseif t==45 then
  call RemoveUnit(udg_A323Unit)
  call A323Mark("removed")
 elseif t==49 then
  if udg_A323Scene==7 then
   call A323Mark("complete")
   call PreloadGenEnd("@OUTPUT@")
   call PauseTimer(udg_A323Timer)
  endif
  set udg_A323Scene=udg_A323Scene+1
 endif
 if t<45 then
  call A323Mark("sample")
 endif
 set udg_A323Tick=udg_A323Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call FogEnable(false)
 call FogMaskEnable(false)
 set udg_A323Timer=CreateTimer()
 call TimerStart(udg_A323Timer,0.1,true,function A323Tick)
 call A323Mark("start")
endfunction
