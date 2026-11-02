globals
 timer udg_M374Timer=null
 unit udg_M374Unit=null
 integer udg_M374Tick=0
 integer udg_M374Scene=0
 integer udg_M374Ability='Abrf'
 integer udg_M374Setting=-1
endglobals
function M374Mark takes string label, boolean ok returns nothing
 local integer accepted=0
 local integer invisible=0
 if ok then
  set accepted=1
 endif
 if IsUnitInvisible(udg_M374Unit,Player(1)) then
  set invisible=1
 endif
 call Preload("M374 setting="+I2S(udg_M374Setting)+" invisible="+I2S(invisible)+" tick="+I2S(udg_M374Tick)+" scene="+I2S(udg_M374Scene)+" label="+label+" accepted="+I2S(accepted)+" type="+I2S(GetUnitTypeId(udg_M374Unit))+" morph="+I2S(GetUnitAbilityLevel(udg_M374Unit,udg_M374Ability))+" passive="+I2S(GetUnitAbilityLevel(udg_M374Unit,'Apiv'))+" order="+I2S(GetUnitCurrentOrder(udg_M374Unit)))
 call PreloadGenEnd("morph374.txt")
endfunction
function M374Tick takes nothing returns nothing
 local integer age=ModuloInteger(udg_M374Tick,70)
 local integer family=udg_M374Scene/3
 local integer mode=ModuloInteger(udg_M374Scene,3)
 if age==0 then
  if family==0 then
   set udg_M374Ability='Abrf'
   if mode==2 then
    set udg_M374Unit=CreateUnit(Player(0),'edcm',640,1024,0)
   else
    set udg_M374Unit=CreateUnit(Player(0),'edoc',640,1024,0)
   endif
  else
   set udg_M374Ability='Arav'
   if mode==2 then
    set udg_M374Unit=CreateUnit(Player(0),'edtm',640,1024,0)
   else
    set udg_M374Unit=CreateUnit(Player(0),'edot',640,1024,0)
   endif
  endif
  call SetUnitAcquireRange(udg_M374Unit,0)
  call SetUnitState(udg_M374Unit,UNIT_STATE_MANA,1000)
  call M374Mark("created",true)
 elseif age==1 then
  call M374Mark("add-morph",UnitAddAbility(udg_M374Unit,udg_M374Ability))
  call UnitMakeAbilityPermanent(udg_M374Unit,true,udg_M374Ability)
 elseif age==2 and mode==0 then
  call M374Mark("add-passive-before",UnitAddAbility(udg_M374Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_M374Unit,true,'Apiv')
 elseif age==12 and mode!=2 then
  if family==0 then
   call M374Mark("forward",IssueImmediateOrder(udg_M374Unit,"bearform"))
  else
   call M374Mark("forward",IssueImmediateOrder(udg_M374Unit,"ravenform"))
  endif
 elseif age==22 and mode!=0 then
  call M374Mark("add-passive-after",UnitAddAbility(udg_M374Unit,'Apiv'))
  call UnitMakeAbilityPermanent(udg_M374Unit,true,'Apiv')
 elseif age==35 then
  if family==0 then
   call M374Mark("reverse",IssueImmediateOrder(udg_M374Unit,"unbearform"))
  else
   call M374Mark("reverse",IssueImmediateOrder(udg_M374Unit,"unravenform"))
  endif
 elseif age==60 then
  call M374Mark("remove-passive",UnitRemoveAbility(udg_M374Unit,'Apiv'))
 elseif age==65 then
  call RemoveUnit(udg_M374Unit)
  call M374Mark("cleanup",true)
 elseif age==69 then
  if udg_M374Scene==5 then
   call M374Mark("complete",true)
   call PauseTimer(udg_M374Timer)
  endif
  set udg_M374Scene=udg_M374Scene+1
 endif
 if age<65 then
  call M374Mark("sample",true)
 endif
 set udg_M374Tick=udg_M374Tick+1
endfunction
function PathProbeInit takes nothing returns nothing
 call PreloadGenClear()
 call PreloadGenStart()
 call SetRandomSeed(1374)
 call FogEnable(false)
 call FogMaskEnable(false)
 call Preload("M374 tick=0 label=start")
 set udg_M374Timer=CreateTimer()
 call TimerStart(udg_M374Timer,0.1,true,function M374Tick)
endfunction
