globals
 timer udg_S366Timer=null
 unit udg_S366Unit=null
 integer udg_S366Tick=0
 integer udg_S366Scene=0
endglobals
function S366Mark takes string label, boolean ok returns nothing
 local integer accepted=0
 if ok then
  set accepted=1
 endif
 call Preload("S366 tick="+I2S(udg_S366Tick)+" scene="+I2S(udg_S366Scene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_S366Unit))+" first="+I2S(GetUnitAbilityLevel(udg_S366Unit,'Ab61'))+" life="+R2S(GetWidgetLife(udg_S366Unit))+" order="+I2S(GetUnitCurrentOrder(udg_S366Unit)))
 call PreloadGenEnd("separation323.txt")
endfunction
function S366Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_S366Tick,50)
 if age==0 then
  if ModuloInteger(udg_S366Scene,2)==0 then
   set udg_S366Unit=CreateUnit(Player(0),'ucry',640,1024,0)
  else
   set udg_S366Unit=CreateUnit(Player(0),'ucrm',640,1024,0)
  endif
  call S366Mark("created",true)
 elseif age==1 then
  call S366Mark("add-first",UnitAddAbility(udg_S366Unit,'Ab61'))
  call UnitMakeAbilityPermanent(udg_S366Unit,true,'Ab61')
 elseif age==3 then
  if ModuloInteger(udg_S366Scene,2)==0 then
   call S366Mark("burrow",IssueImmediateOrder(udg_S366Unit,"burrow"))
  else
   call S366Mark("unburrow",IssueImmediateOrder(udg_S366Unit,"unburrow"))
  endif
 elseif age==4 then
  if udg_S366Scene<2 then
   call S366Mark("remove-during-hold",UnitRemoveAbility(udg_S366Unit,'Ab61'))
  else
   call KillUnit(udg_S366Unit)
   call S366Mark("kill-during-hold",true)
  endif
 elseif age==45 then
  call RemoveUnit(udg_S366Unit)
  call S366Mark("cleanup",true)
 elseif age==49 then
  if udg_S366Scene==3 then
   call S366Mark("complete",true)
   call PauseTimer(udg_S366Timer)
  endif
  set udg_S366Scene=udg_S366Scene+1
 endif
 if age<45 then
  call S366Mark("sample",true)
 endif
 set udg_S366Tick=udg_S366Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1366)
 call SetPlayerTechResearched(Player(0),'Rubu',1)
 call FogEnable(false)
 call FogMaskEnable(false)
 call Preload("S366 tick=0 label=start")
 set udg_S366Timer=CreateTimer()
 call TimerStart(udg_S366Timer,0.1,true,function S366Tick)
endfunction
