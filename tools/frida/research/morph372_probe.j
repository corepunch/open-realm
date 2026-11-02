globals
 timer udg_M372Timer=null
 unit udg_M372Unit=null
 integer udg_M372Tick=0
 integer udg_M372Scene=0
 integer udg_M372Ability='Asb1'
endglobals
function M372Mark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_M372Unit,Player(1)) then
  set invisible=1
 endif
 call Preload("M372 invisible="+I2S(invisible)+" tick="+I2S(udg_M372Tick)+" scene="+I2S(udg_M372Scene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_M372Unit))+" submerge="+I2S(GetUnitAbilityLevel(udg_M372Unit,udg_M372Ability))+" passive="+I2S(GetUnitAbilityLevel(udg_M372Unit,'Apiv'))+" order="+I2S(GetUnitCurrentOrder(udg_M372Unit)))
 call PreloadGenEnd("separation323.txt")
endfunction
function M372Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_M372Tick,60)
 if age==0 then
  set udg_M372Ability='Asb1'
  if udg_M372Scene==2 then
   set udg_M372Unit=CreateUnit(Player(0),'nmys',640,1024,0)
  elseif udg_M372Scene==3 then
   set udg_M372Ability='Asb2'
   set udg_M372Unit=CreateUnit(Player(0),'nnrg',640,1024,0)
  elseif udg_M372Scene==4 then
   set udg_M372Ability='Asb3'
   set udg_M372Unit=CreateUnit(Player(0),'nsnp',640,1024,0)
  else
   set udg_M372Unit=CreateUnit(Player(0),'nmyr',640,1024,0)
  endif
  call SetUnitAcquireRange(udg_M372Unit,0)
  call SetUnitState(udg_M372Unit,UNIT_STATE_MANA,1000)
  call M372Mark("created",true)
 elseif age==1 then
  call M372Mark("add-submerge",UnitAddAbility(udg_M372Unit,udg_M372Ability))
  call UnitMakeAbilityPermanent(udg_M372Unit,true,udg_M372Ability)
 elseif age==2 and udg_M372Scene!=1 and udg_M372Scene!=2 then
  call M372Mark("add-passive-before",UnitAddAbility(udg_M372Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_M372Unit,true,'Apiv')
 elseif age==12 and udg_M372Scene!=2 then
  call M372Mark("submerge",IssueImmediateOrder(udg_M372Unit,"submerge"))
 elseif age==20 and (udg_M372Scene==1 or udg_M372Scene==2) then
  call M372Mark("add-passive-after",UnitAddAbility(udg_M372Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_M372Unit,true,'Apiv')
 elseif age==30 then
  call M372Mark("unsubmerge",IssueImmediateOrder(udg_M372Unit,"unsubmerge"))
 elseif age==50 then
  call M372Mark("remove-passive",UnitRemoveAbility(udg_M372Unit,'Apiv'))
 elseif age==55 then
  call RemoveUnit(udg_M372Unit)
  call M372Mark("cleanup",true)
 elseif age==59 then
  if udg_M372Scene==4 then
   call M372Mark("complete",true)
   call PauseTimer(udg_M372Timer)
  endif
  set udg_M372Scene=udg_M372Scene+1
 endif
 if age<55 then
  call M372Mark("sample",true)
 endif
 set udg_M372Tick=udg_M372Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1372)
 call FogEnable(false)
 call FogMaskEnable(false)
 call Preload("M372 tick=0 label=start")
 set udg_M372Timer=CreateTimer()
 call TimerStart(udg_M372Timer,0.1,true,function M372Tick)
endfunction
