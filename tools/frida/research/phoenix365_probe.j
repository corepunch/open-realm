globals
 timer udg_P365Timer=null
 unit udg_P365Unit=null
 integer udg_P365Tick=0
 integer udg_P365Scene=0
endglobals
function P365Mark takes string label returns nothing
 call Preload("P365 tick="+I2S(udg_P365Tick)+" scene="+I2S(udg_P365Scene)+" label="+label+" type="+I2S(GetUnitTypeId(udg_P365Unit))+" order="+I2S(GetUnitCurrentOrder(udg_P365Unit))+" life="+R2SW(GetWidgetLife(udg_P365Unit),0,4))
 if label!="start" then
  call PreloadGenEnd("separation323.txt")
 endif
endfunction
function P365Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_P365Tick,200)
 local boolean ok=false
 if age==0 then
  set udg_P365Unit=CreateUnit(Player(0),'hphx',640,1024,0)
  set ok=UnitAddAbility(udg_P365Unit,'Aphx')
  call UnitMakeAbilityPermanent(udg_P365Unit,true,'Aphx')
  call SetUnitAcquireRange(udg_P365Unit,0)
  if ok then
   call P365Mark("attach-accepted")
  else
   call P365Mark("attach-rejected")
  endif
 elseif age==2 then
  if udg_P365Scene==0 then
   call SetUnitState(udg_P365Unit,UNIT_STATE_LIFE,1)
   call P365Mark("set-life")
  elseif udg_P365Scene==1 then
   call KillUnit(udg_P365Unit)
   call P365Mark("kill")
  else
   set ok=UnitDamageTarget(udg_P365Unit,udg_P365Unit,10000,true,false,ATTACK_TYPE_NORMAL,DAMAGE_TYPE_NORMAL,WEAPON_TYPE_WHOKNOWS)
   if ok then
    call P365Mark("damage-accepted")
   else
    call P365Mark("damage-rejected")
   endif
  endif
 elseif age==197 then
  call RemoveUnit(udg_P365Unit)
  call P365Mark("cleanup")
 elseif age==199 then
  if udg_P365Scene==2 then
   call P365Mark("complete")
   call PauseTimer(udg_P365Timer)
  endif
  set udg_P365Scene=udg_P365Scene+1
 endif
 if age<197 then
  call P365Mark("sample")
 endif
 set udg_P365Tick=udg_P365Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1365)
 call FogEnable(false)
 call FogMaskEnable(false)
 call SetTimeOfDayScale(0)
 call SetFloatGameState(GAME_STATE_TIME_OF_DAY,12)
 call P365Mark("start")
 set udg_P365Timer=CreateTimer()
 call TimerStart(udg_P365Timer,0.1,true,function P365Tick)
endfunction
